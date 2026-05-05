from __future__ import annotations

import fcntl
import os
import pty
import subprocess
import sys
import time
from pathlib import Path
from typing import TextIO

from .state import (
    Monitor,
    command_path_for,
    label_for,
    plain_label_for,
    spool_path_for,
)


def ensure_command_fifo(env: str) -> tuple[int, int]:
    path = command_path_for(env)
    try:
        os.mkfifo(path, 0o600)
    except FileExistsError:
        pass

    read_fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
    keepalive_fd = os.open(path, os.O_WRONLY | os.O_NONBLOCK)
    return read_fd, keepalive_fd


def baud_value(baud: str | None) -> str:
    if baud == "h" or baud is None:
        return "921600"
    if baud == "l":
        return "115200"
    return baud


def spawn_monitor(
    root: Path,
    env: str,
    baud: str | None,
    width: int,
    strip_ansi: bool,
    spool_writer: TextIO,
    child_env: dict[str, str],
) -> Monitor:
    command = [
        str(root / ".venv" / "bin" / "pio"),
        "device",
        "monitor",
        "-b",
        baud_value(baud),
        "--raw",
        "--port",
        child_env["UPLOAD_PORT"],
    ]

    pid, fd = pty.fork()
    if pid == 0:
        os.execve(command[0], command, child_env)

    os.set_blocking(fd, False)
    command_fd, command_keepalive_fd = ensure_command_fifo(env)
    print(f"{env}: {' '.join(command)}", file=sys.stderr)
    return Monitor(
        env=env,
        label=label_for(env, width, sys.stdout.isatty() and not strip_ansi),
        plain_label=plain_label_for(env, width),
        pid=pid,
        fd=fd,
        spool_path=spool_path_for(env),
        spool_writer=spool_writer,
        command_fd=command_fd,
        command_keepalive_fd=command_keepalive_fd,
    )


def attach_spool(env: str, width: int, strip_ansi: bool) -> Monitor:
    path = spool_path_for(env)
    deadline = time.monotonic() + 2
    while not path.exists() and time.monotonic() < deadline:
        time.sleep(0.05)

    if not path.exists():
        raise RuntimeError(
            f"Monitor for {env!r} is locked, but shared spool {path} does not exist"
        )

    spool_reader = path.open("r", encoding="utf-8", errors="replace")
    spool_reader.seek(0, os.SEEK_END)
    print(f"{env}: attached to {path}", file=sys.stderr)
    return Monitor(
        env=env,
        label=label_for(env, width, sys.stdout.isatty() and not strip_ansi),
        plain_label=plain_label_for(env, width),
        spool_path=path,
        spool_reader=spool_reader,
    )


def lock_path_for(env: str) -> Path:
    from .state import LOCK_DIR, safe_env_name

    return LOCK_DIR / f"monitor-multi.{safe_env_name(env)}.lock"


def try_lock_instance(env: str) -> TextIO | None:
    lock_file = lock_path_for(env).open("w")
    try:
        fcntl.flock(lock_file, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        lock_file.close()
        return None

    return lock_file


def open_spool_writer(env: str) -> TextIO:
    return spool_path_for(env).open("w", encoding="utf-8", buffering=1)


def open_log_file(path: Path | None) -> TextIO | None:
    if path is None:
        return None

    return path.open("a", buffering=1)


def write_to_monitor(monitor: Monitor, command: str) -> None:
    if monitor.fd is None:
        raise RuntimeError(f"monitor {monitor.env!r} is not owned by this process")

    os.write(monitor.fd, f"{command}\n".encode())


def write_bytes_to_monitor(monitor: Monitor, data: bytes) -> None:
    if monitor.fd is None:
        raise RuntimeError(f"monitor {monitor.env!r} is not owned by this process")

    os.write(monitor.fd, data)


def write_to_owner_fifo(env: str, command: str) -> None:
    path = command_path_for(env)
    fd = os.open(path, os.O_WRONLY | os.O_NONBLOCK)
    try:
        os.write(fd, f"{command}\n".encode())
    finally:
        os.close(fd)


def close_fds(monitors: list[Monitor]) -> None:
    for monitor in monitors:
        for fd in (monitor.fd, monitor.command_fd, monitor.command_keepalive_fd):
            if fd is None:
                continue
            try:
                os.close(fd)
            except OSError:
                pass


def close_spools(monitors: list[Monitor]) -> None:
    for monitor in monitors:
        for spool in (monitor.spool_reader, monitor.spool_writer):
            if spool is None:
                continue
            try:
                spool.close()
            except OSError:
                pass
