from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path


def git_root() -> Path:
    result = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        check=True,
        capture_output=True,
        text=True,
    )
    return Path(result.stdout.strip())


def monitor_env() -> dict[str, str]:
    env = os.environ.copy()
    env.pop("RESET", None)
    env.pop("UPLOAD", None)
    env.pop("UPLOADFS", None)
    return env


def tool_env(root: Path) -> dict[str, str]:
    env = monitor_env()
    venv_bin = root / ".venv" / "bin"
    env["PATH"] = f"{venv_bin}:{env.get('PATH', '')}"
    return env


def parse_exported_env(output: str) -> dict[str, str]:
    parsed: dict[str, str] = {}
    pattern = re.compile(r'^export ([A-Za-z_][A-Za-z0-9_]*)="(.*)"$')
    for line in output.splitlines():
        match = pattern.match(line.strip())
        if match:
            parsed[match.group(1)] = match.group(2)

    return parsed


def selected_port_env(root: Path, env_name: str) -> dict[str, str]:
    env_by_name = selected_port_envs(root, [env_name])
    return env_by_name[env_name]


def selected_port_envs(root: Path, envs: list[str]) -> dict[str, dict[str, str]]:
    pending = []
    for env_name in envs:
        env = tool_env(root)
        process = subprocess.Popen(
            [
                str(root / "bin" / "select_port.py"),
                env_name,
            ],
            cwd=root,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        pending.append((env_name, env, process))

    resolved: dict[str, dict[str, str]] = {}
    failures: list[tuple[str, int, object]] = []
    for env_name, env, process in pending:
        stdout, stderr = process.communicate()
        if stdout:
            env.update(parse_exported_env(stdout))

        if stderr:
            print(stderr, end="", file=sys.stderr)

        if process.returncode != 0:
            if stdout:
                print(stdout, end="", file=sys.stderr)
            failures.append((env_name, process.returncode, process.args))
            continue

        resolved[env_name] = env

    if failures:
        _, returncode, args = failures[0]
        raise subprocess.CalledProcessError(returncode, args)

    return resolved
