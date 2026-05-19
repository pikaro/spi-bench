from __future__ import annotations

import subprocess
import sys
from pathlib import Path

from .state import Config


def run_pio_target(
    root: Path,
    env_name: str,
    target: str,
    env: dict[str, str],
) -> subprocess.Popen[bytes]:
    command = [
        str(root / '.venv' / 'bin' / 'pio'),
        'run',
        '-e',
        env_name,
        '--target',
        target,
    ]
    print(f'{env_name}: {" ".join(command)}', file=sys.stderr)
    return subprocess.Popen(command, cwd=root, env=env)


def command_display(command: object) -> str:
    if isinstance(command, (list, tuple)):
        return ' '.join(str(part) for part in command)
    return str(command)


def run_pio_target_stage(
    root: Path,
    envs: list[str],
    target: str,
    env_by_name: dict[str, dict[str, str]],
    parallel: bool = False,
) -> None:
    if parallel:
        processes = [
            (env_name, run_pio_target(root, env_name, target, env_by_name[env_name]))
            for env_name in envs
        ]
        failures: list[tuple[str, int]] = []
        for env_name, process in processes:
            returncode = process.wait()
            if returncode != 0:
                failures.append((env_name, returncode))

        if failures:
            failed_env, returncode = failures[0]
            raise subprocess.CalledProcessError(
                returncode,
                f'pio run -e {failed_env} --target {target}',
            )
        return

    for env_name in envs:
        process = run_pio_target(root, env_name, target, env_by_name[env_name])
        returncode = process.wait()
        if returncode != 0:
            raise subprocess.CalledProcessError(
                returncode,
                f'pio run -e {env_name} --target {target}',
            )


def run_pre_ops(
    root: Path,
    envs: list[str],
    config: Config,
    env_by_name: dict[str, dict[str, str]],
) -> None:
    if config.upload:
        print(
            'monitor-multi: uploading all requested environments sequentially',
            file=sys.stderr,
        )
        run_pio_target_stage(root, envs, 'upload', env_by_name)

    if config.uploadfs:
        print(
            'monitor-multi: uploading filesystems for all requested environments sequentially',
            file=sys.stderr,
        )
        run_pio_target_stage(root, envs, 'uploadfs', env_by_name)

    if config.reset:
        print(
            'monitor-multi: resetting all requested environments in parallel',
            file=sys.stderr,
        )
        run_pio_target_stage(root, envs, 'reset', env_by_name, parallel=True)
