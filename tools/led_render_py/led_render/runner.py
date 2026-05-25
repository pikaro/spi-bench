from __future__ import annotations

import pathlib
import subprocess


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[3]


def run_render(
    config: str | pathlib.Path,
    output: str | pathlib.Path,
    *,
    frames: str | None = None,
    animation: str | None = None,
    fps: int | None = None,
    mode: str | None = None,
    include_scratch: bool = False,
    root: str | pathlib.Path | None = None,
) -> pathlib.Path:
    root_path = pathlib.Path(root) if root is not None else repo_root()
    output_path = pathlib.Path(output)
    command = [
        str(root_path / "bin" / "led-render"),
        "--config",
        str(config),
        "--output",
        str(output_path),
    ]
    if frames is not None:
        command += ["--frames", frames]
    if animation is not None:
        command += ["--animation", animation]
    if fps is not None:
        command += ["--fps", str(fps)]
    if mode is not None:
        command += ["--mode", mode]
    if include_scratch:
        command += ["--include-scratch"]

    subprocess.run(command, check=True)
    return output_path
