from __future__ import annotations

import re
import sys

from .state import BufferedLine, Monitor, Runtime, SummaryEntry, plain_label_for

ESP_LOG_TIME_RE = re.compile(r'\b((?:[A-Z]{3}|[A-Z]) )\(\d+\)(?: \(\d+\))?')


def strip_ansi(text: str) -> str:
    from .state import ANSI_RE

    return ANSI_RE.sub("", text)


def format_line(line: str, runtime: Runtime) -> str:
    if runtime.config.strip_ansi:
        return strip_ansi(line)
    return line


def prefixed_line(label: str, line: str) -> str:
    return f"{label} | {line}"


def display_line(monitor: Monitor, line: str, runtime: Runtime) -> str:
    line = format_line(line, runtime)
    if runtime.config.prefix_output:
        return prefixed_line(monitor.label, line)
    return line


def plain_display_line(monitor: Monitor, line: str, runtime: Runtime) -> str:
    line = format_line(line, runtime)
    if runtime.config.prefix_output:
        return prefixed_line(monitor.plain_label, line)
    return line


def filter_text(monitor: Monitor, line: str, runtime: Runtime) -> str:
    text = strip_ansi(line)
    if runtime.config.prefix_output:
        return prefixed_line(monitor.plain_label, text)
    return text


def is_excluded(monitor: Monitor, line: str, runtime: Runtime) -> bool:
    text = filter_text(monitor, line, runtime)
    return any(pattern.search(text) for pattern in runtime.config.exclude)


def matches_filters(monitor: Monitor, line: str, runtime: Runtime) -> bool:
    text = filter_text(monitor, line, runtime)
    if runtime.config.include and not any(
        pattern.search(text) for pattern in runtime.config.include
    ):
        return False

    return not any(pattern.search(text) for pattern in runtime.config.exclude)


def summary_key(monitor: Monitor, line: str) -> tuple[str, str]:
    text = strip_ansi(line)
    text = re.sub(r"\b\d+(?:\.\d+)?\b", "n", text)
    return (monitor.env, text)


def repeat_key(monitor: Monitor, line: str, runtime: Runtime) -> str:
    text = strip_ansi(display_line(monitor, line, runtime))
    return ESP_LOG_TIME_RE.sub(r'\1(t)', text)


def print_output_line(monitor: Monitor, line: str, runtime: Runtime) -> None:
    output_line = display_line(monitor, line, runtime)
    print(output_line)
    runtime.last_printed_repeat_key = repeat_key(monitor, line, runtime)
    runtime.printed_lines += 1

    if (
        runtime.config.max_lines is not None
        and runtime.printed_lines >= runtime.config.max_lines
    ):
        runtime.stop_reason = "max line limit reached"


def write_log(monitor: Monitor, line: str, runtime: Runtime) -> None:
    if runtime.log is None:
        return

    print(plain_display_line(monitor, line, runtime), file=runtime.log)


def write_spool(monitor: Monitor, line: str) -> None:
    if monitor.spool_writer is None:
        return

    print(line, file=monitor.spool_writer)


def print_monitor_line(
    monitor: Monitor,
    buffered: BufferedLine,
    runtime: Runtime,
    summarize: bool = False,
) -> None:
    if buffered.number <= monitor.emitted_until:
        return

    output_line = format_line(buffered.text, runtime)
    if summarize and runtime.config.summary:
        key = summary_key(monitor, buffered.text)
        entry = runtime.summary.get(key)
        if entry is not None:
            entry.count += 1
            entry.last = output_line
            monitor.emitted_until = buffered.number
            return

        runtime.summary[key] = SummaryEntry(
            count=1,
            first=output_line,
            last=output_line,
        )

    if runtime.config.suppress_repeats:
        key = repeat_key(monitor, buffered.text, runtime)
        if key == runtime.last_printed_repeat_key:
            monitor.emitted_until = buffered.number
            return

    print_output_line(monitor, buffered.text, runtime)
    monitor.emitted_until = buffered.number


def remember_before(monitor: Monitor, buffered: BufferedLine, runtime: Runtime) -> None:
    if runtime.config.before <= 0:
        return

    monitor.before.append(buffered)
    while len(monitor.before) > runtime.config.before:
        monitor.before.popleft()


def emit_context_match(
    monitor: Monitor,
    buffered: BufferedLine,
    runtime: Runtime,
) -> None:
    for previous in monitor.before:
        if is_excluded(monitor, previous.text, runtime):
            continue
        print_monitor_line(monitor, previous, runtime)
        if runtime.stop_reason:
            return

    print_monitor_line(monitor, buffered, runtime, summarize=True)
    if runtime.stop_reason:
        return

    monitor.after_remaining = runtime.config.after


def emit_context_line(
    monitor: Monitor,
    buffered: BufferedLine,
    runtime: Runtime,
) -> None:
    if monitor.after_remaining <= 0:
        return

    monitor.after_remaining -= 1
    if is_excluded(monitor, buffered.text, runtime):
        return

    print_monitor_line(monitor, buffered, runtime)


def emit_line(monitor: Monitor, line: str, runtime: Runtime) -> None:
    monitor.line_number += 1
    buffered = BufferedLine(number=monitor.line_number, text=line)

    write_spool(monitor, line)
    write_log(monitor, line, runtime)

    matched = matches_filters(monitor, line, runtime)
    if runtime.config.has_context:
        if matched:
            emit_context_match(monitor, buffered, runtime)
        else:
            emit_context_line(monitor, buffered, runtime)
    elif matched:
        print_monitor_line(monitor, buffered, runtime, summarize=True)

    remember_before(monitor, buffered, runtime)


def emit_buffered_lines(
    monitor: Monitor,
    runtime: Runtime,
    final: bool = False,
) -> None:
    while "\n" in monitor.buffer:
        line, monitor.buffer = monitor.buffer.split("\n", 1)
        if line:
            emit_line(monitor, line, runtime)
            if runtime.stop_reason:
                sys.stdout.flush()
                return

    if final and monitor.buffer:
        emit_line(monitor, monitor.buffer, runtime)
        monitor.buffer = ""

    sys.stdout.flush()


def print_summary(runtime: Runtime) -> None:
    if not runtime.config.summary:
        return

    repeated = [
        (env, entry) for (env, _), entry in runtime.summary.items() if entry.count > 1
    ]
    if not repeated:
        return

    width = max(len(env) for env in runtime.config.envs)
    print("monitor-multi summary:", file=sys.stderr)
    for env, entry in repeated:
        label = plain_label_for(env, width)
        lines = [f"repeated {entry.count}x", f"first: {entry.first}"]
        if entry.last != entry.first:
            lines.append(f"last:  {entry.last}")

        for line in lines:
            if runtime.config.prefix_output:
                print(prefixed_line(label, line))
            else:
                print(line)
