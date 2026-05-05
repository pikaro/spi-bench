from __future__ import annotations

import errno
import os
import selectors
import signal
import subprocess
import sys
import termios
import time

from .cli import parse_args
from .output import emit_buffered_lines, print_summary
from .ports import git_root, selected_port_envs
from .pre_ops import command_display, run_pre_ops
from .processes import (
    attach_spool,
    close_fds,
    close_spools,
    open_log_file,
    open_spool_writer,
    spawn_monitor,
    try_lock_instance,
    write_bytes_to_monitor,
    write_to_monitor,
    write_to_owner_fifo,
)
from .state import STDIN_DIRECT, STDIN_LINE, Config, Monitor, Runtime


def read_monitor(monitor: Monitor, runtime: Runtime) -> bool:
    if monitor.fd is None:
        return False

    try:
        data = os.read(monitor.fd, 4096)
    except BlockingIOError:
        return True
    except OSError as exc:
        if exc.errno != errno.EIO:
            raise
        data = b""

    if not data:
        return False

    text = data.decode(errors="replace")
    monitor.buffer += text.replace("\r\n", "\n").replace("\r", "\n")
    emit_buffered_lines(monitor, runtime)
    return True


def poll_spool(monitor: Monitor, runtime: Runtime) -> None:
    if monitor.spool_reader is None:
        return

    if monitor.spool_path is not None:
        try:
            if monitor.spool_path.stat().st_size < monitor.spool_reader.tell():
                monitor.spool_reader.seek(0)
        except FileNotFoundError:
            return

    monitor.spool_reader.seek(monitor.spool_reader.tell())
    while True:
        line = monitor.spool_reader.readline()
        if not line:
            return

        monitor.buffer += line.replace("\r\n", "\n").replace("\r", "\n")
        emit_buffered_lines(monitor, runtime)
        if runtime.stop_reason:
            return


def wait_status(pid: int) -> int:
    _, status = os.waitpid(pid, 0)
    if os.WIFEXITED(status):
        return os.WEXITSTATUS(status)
    if os.WIFSIGNALED(status):
        return 128 + os.WTERMSIG(status)
    return 1


def terminate(monitors: list[Monitor]) -> None:
    for monitor in monitors:
        if monitor.pid is None:
            continue
        try:
            os.killpg(monitor.pid, signal.SIGTERM)
        except (PermissionError, ProcessLookupError):
            try:
                os.kill(monitor.pid, signal.SIGTERM)
            except (PermissionError, ProcessLookupError):
                pass


def reap(monitors: list[Monitor]) -> None:
    deadline = time.monotonic() + 2
    pending = {monitor.pid for monitor in monitors if monitor.pid is not None}

    while pending and time.monotonic() < deadline:
        for pid in list(pending):
            try:
                waited_pid, _ = os.waitpid(pid, os.WNOHANG)
            except ChildProcessError:
                pending.remove(pid)
                continue

            if waited_pid:
                pending.remove(pid)

        if pending:
            time.sleep(0.05)


def request_stop(_signum: int, _frame: object) -> None:
    raise KeyboardInterrupt


def send_command_to_env(
    target: str,
    command: str,
    by_fd: dict[int, Monitor],
    runtime: Runtime,
    announce: bool,
) -> None:
    by_env = {monitor.env: monitor for monitor in by_fd.values()}
    monitor = by_env.get(target)
    if monitor is not None:
        try:
            write_to_monitor(monitor, command)
        except (OSError, RuntimeError) as exc:
            print(f"monitor-multi: failed to send to {target}: {exc}", file=sys.stderr)
            return
        if announce:
            print(f"monitor-multi: sent to {target}: {command}", file=sys.stderr)
        return

    if target not in runtime.config.envs:
        print(f"monitor-multi: no active monitor for {target!r}", file=sys.stderr)
        return

    try:
        write_to_owner_fifo(target, command)
    except OSError as exc:
        print(
            f"monitor-multi: failed to send to owner for {target}: {exc}",
            file=sys.stderr,
        )
        return

    if announce:
        print(f"monitor-multi: sent to owner for {target}: {command}", file=sys.stderr)


def route_command(line: str, by_fd: dict[int, Monitor], runtime: Runtime) -> None:
    line = line.rstrip("\n")
    if not line:
        return

    if runtime.config.single_env and not line.startswith("!"):
        send_command_to_env(runtime.config.envs[0], line, by_fd, runtime, announce=False)
        return

    if not line.startswith("!"):
        if not runtime.ignored_stdin_warned:
            print(
                "monitor-multi: ignoring stdin; use !<env> <command>",
                file=sys.stderr,
            )
            runtime.ignored_stdin_warned = True
        return

    target, separator, command = line[1:].partition(" ")
    command = command.lstrip()
    if not target or not separator or not command:
        print("monitor-multi: command syntax is !<env> <command>", file=sys.stderr)
        return

    send_command_to_env(target, command, by_fd, runtime, announce=True)


def read_command_fifo(monitor: Monitor) -> None:
    if monitor.command_fd is None:
        return

    try:
        data = os.read(monitor.command_fd, 4096)
    except BlockingIOError:
        return

    if not data:
        return

    monitor.command_buffer += data.decode(errors="replace")
    while "\n" in monitor.command_buffer:
        command, monitor.command_buffer = monitor.command_buffer.split("\n", 1)
        command = command.strip()
        if command:
            try:
                write_to_monitor(monitor, command)
            except (OSError, RuntimeError) as exc:
                print(
                    f"monitor-multi: failed to route command to {monitor.env}: {exc}",
                    file=sys.stderr,
                )


def read_stdin_line(
    selector: selectors.BaseSelector,
    by_fd: dict[int, Monitor],
    runtime: Runtime,
) -> None:
    line = sys.stdin.readline()
    if not line:
        try:
            selector.unregister(sys.stdin)
        except KeyError:
            pass
        return

    route_command(line, by_fd, runtime)


def read_stdin_direct(
    selector: selectors.BaseSelector,
    monitor: Monitor,
) -> None:
    try:
        data = os.read(sys.stdin.fileno(), 4096)
    except OSError:
        try:
            selector.unregister(sys.stdin)
        except KeyError:
            pass
        return

    if not data:
        try:
            selector.unregister(sys.stdin)
        except KeyError:
            pass
        return

    try:
        write_bytes_to_monitor(monitor, data)
    except (OSError, RuntimeError) as exc:
        print(f"monitor-multi: failed to write stdin: {exc}", file=sys.stderr)


def enable_direct_stdin(enable: bool) -> list[object] | None:
    if not enable:
        return None

    fd = sys.stdin.fileno()
    original = termios.tcgetattr(fd)
    direct = original.copy()
    direct[6] = original[6].copy()
    direct[3] &= ~(termios.ICANON | termios.ECHO)
    direct[6][termios.VMIN] = 1
    direct[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSADRAIN, direct)
    return original


def restore_stdin(original: list[object] | None) -> None:
    if original is None:
        return

    termios.tcsetattr(sys.stdin.fileno(), termios.TCSADRAIN, original)


def finish(monitors: list[Monitor], runtime: Runtime, code: int) -> int:
    sys.stdout.flush()
    if runtime.stop_reason:
        print(f"monitor-multi: {runtime.stop_reason}", file=sys.stderr)

    print_summary(runtime)
    if runtime.log is not None:
        runtime.log.flush()

    return code


def direct_stdin_monitor(monitors: list[Monitor], config: Config) -> Monitor | None:
    if not config.single_env:
        return None

    owned = [monitor for monitor in monitors if monitor.fd is not None]
    if len(owned) != 1:
        return None

    return owned[0]


def run(monitors: list[Monitor], runtime: Runtime) -> int:
    selector = selectors.DefaultSelector()
    by_fd = {monitor.fd: monitor for monitor in monitors if monitor.fd is not None}
    by_command_fd = {
        monitor.command_fd: monitor
        for monitor in monitors
        if monitor.command_fd is not None
    }
    followers = [monitor for monitor in monitors if monitor.spool_reader is not None]
    exit_codes: list[int] = []
    deadline = (
        None
        if runtime.config.timeout is None
        else time.monotonic() + runtime.config.timeout
    )
    direct_monitor = direct_stdin_monitor(monitors, runtime.config)
    original_stdin = None

    for fd, monitor in by_fd.items():
        selector.register(fd, selectors.EVENT_READ, monitor)

    for fd, monitor in by_command_fd.items():
        selector.register(fd, selectors.EVENT_READ, monitor)

    if sys.stdin.isatty():
        original_stdin = enable_direct_stdin(direct_monitor is not None)
        selector.register(
            sys.stdin,
            selectors.EVENT_READ,
            STDIN_DIRECT if direct_monitor is not None else STDIN_LINE,
        )

    try:
        try:
            while by_fd or followers:
                select_timeout = None
                if deadline is not None:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        runtime.stop_reason = "timeout reached"
                        break
                    select_timeout = remaining

                if followers:
                    select_timeout = (
                        0.1 if select_timeout is None else min(select_timeout, 0.1)
                    )

                if selector.get_map():
                    events = selector.select(select_timeout)
                else:
                    time.sleep(0.1 if select_timeout is None else select_timeout)
                    events = []

                for key, _ in events:
                    if key.data is STDIN_LINE:
                        read_stdin_line(selector, by_fd, runtime)
                        continue

                    if key.data is STDIN_DIRECT:
                        if direct_monitor is not None:
                            read_stdin_direct(selector, direct_monitor)
                        continue

                    if key.fd in by_command_fd:
                        read_command_fifo(by_command_fd[key.fd])
                        continue

                    monitor = key.data
                    if read_monitor(monitor, runtime):
                        if runtime.stop_reason:
                            break
                        continue

                    if monitor.fd is not None:
                        selector.unregister(monitor.fd)
                        os.close(monitor.fd)
                        by_fd.pop(monitor.fd, None)

                    if monitor.command_fd is not None:
                        selector.unregister(monitor.command_fd)
                        by_command_fd.pop(monitor.command_fd, None)

                    emit_buffered_lines(monitor, runtime, final=True)
                    if monitor.pid is not None:
                        exit_codes.append(wait_status(monitor.pid))

                if runtime.stop_reason:
                    break

                for follower in followers:
                    poll_spool(follower, runtime)
                    if runtime.stop_reason:
                        break

                if runtime.stop_reason:
                    break
        except KeyboardInterrupt:
            runtime.stop_reason = "interrupted"
            terminate(list(by_fd.values()))
            reap(list(by_fd.values()))
            close_fds(list(by_fd.values()))
            return finish(monitors, runtime, 130)
    finally:
        restore_stdin(original_stdin)

    if runtime.stop_reason:
        terminate(list(by_fd.values()))
        reap(list(by_fd.values()))
        close_fds(list(by_fd.values()))
        return finish(monitors, runtime, 0)

    return finish(
        monitors, runtime, next((code for code in exit_codes if code != 0), 0)
    )


def main() -> int:
    config = parse_args(sys.argv[1:])
    root = git_root()
    width = max(len(env) for env in config.envs)
    signal.signal(signal.SIGTERM, request_stop)

    locks = []
    locks_by_env = {}
    monitors: list[Monitor] = []
    log_file = None
    try:
        for env in config.envs:
            lock = try_lock_instance(env)
            if lock is None:
                if config.needs_owner_pre_ops:
                    raise SystemExit(
                        f"Cannot reset or upload {env!r}: another instance "
                        "already owns that monitor"
                    )
                continue

            locks.append(lock)
            locks_by_env[env] = lock

        owned_envs = [env for env in config.envs if env in locks_by_env]
        env_by_name = selected_port_envs(root, owned_envs)

        run_pre_ops(root, owned_envs, config, env_by_name)

        log_file = open_log_file(config.log_file)
        runtime = Runtime(config=config, log=log_file)
        for env in config.envs:
            if env not in locks_by_env:
                monitors.append(attach_spool(env, width, config.strip_ansi))
                continue

            monitors.append(
                spawn_monitor(
                    root,
                    env,
                    config.baud,
                    width,
                    config.strip_ansi,
                    open_spool_writer(env),
                    env_by_name[env],
                )
            )

        return run(monitors, runtime)
    except subprocess.CalledProcessError as exc:
        print(
            "monitor-multi: command failed "
            f"({exc.returncode}): {command_display(exc.cmd)}",
            file=sys.stderr,
        )
        return exc.returncode
    finally:
        close_fds(monitors)
        close_spools(monitors)
        if log_file is not None:
            log_file.close()
        for lock in locks:
            lock.close()
