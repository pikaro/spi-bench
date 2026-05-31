from __future__ import annotations

import re
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import TextIO

LOCK_DIR = Path('/tmp')
BLUE = '\033[34m'
RESET = '\033[0m'
ANSI_RE = re.compile(r'\x1b(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')
STDIN_LINE = object()
STDIN_DIRECT = object()


@dataclass
class BufferedLine:
    number: int
    text: str


@dataclass
class Monitor:
    env: str
    label: str
    plain_label: str
    pid: int | None = None
    fd: int | None = None
    spool_path: Path | None = None
    spool_reader: TextIO | None = None
    spool_writer: TextIO | None = None
    command_fd: int | None = None
    command_keepalive_fd: int | None = None
    buffer: str = ''
    command_buffer: str = ''
    line_number: int = 0
    emitted_until: int = 0
    after_remaining: int = 0
    before: deque[BufferedLine] = field(default_factory=deque)


@dataclass
class Config:
    envs: list[str]
    baud: str | None
    reset: bool
    upload: bool
    uploadfs: bool
    timeout: float | None
    strip_ansi: bool
    include: list[re.Pattern[str]]
    exclude: list[re.Pattern[str]]
    before: int
    after: int
    suppress_repeats: bool
    summary: bool
    max_lines: int | None
    log_file: Path | None
    command: str | None

    @property
    def single_env(self) -> bool:
        return len(self.envs) == 1

    @property
    def prefix_output(self) -> bool:
        return not self.single_env

    @property
    def has_context(self) -> bool:
        return self.before > 0 or self.after > 0

    @property
    def needs_owner_pre_ops(self) -> bool:
        return self.reset or self.upload or self.uploadfs


@dataclass
class SummaryEntry:
    count: int
    first: str
    last: str


@dataclass
class Runtime:
    config: Config
    log: TextIO | None
    printed_lines: int = 0
    stop_reason: str | None = None
    ignored_stdin_warned: bool = False
    summary: dict[tuple[str, str], SummaryEntry] = field(default_factory=dict)
    last_printed_repeat_key: str | None = None


def plain_label_for(env: str, width: int) -> str:
    return f'{env:<{width}}'


def label_for(env: str, width: int, color: bool) -> str:
    label = plain_label_for(env, width)
    if color:
        return f'{BLUE}{label}{RESET}'
    return label


def safe_env_name(env: str) -> str:
    return re.sub(r'[^A-Za-z0-9_.-]+', '_', env)


def spool_path_for(env: str) -> Path:
    return LOCK_DIR / f'monitor-multi.{safe_env_name(env)}.spool'


def command_path_for(env: str) -> Path:
    return LOCK_DIR / f'monitor-multi.{safe_env_name(env)}.cmd'
