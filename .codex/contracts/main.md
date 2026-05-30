# Project contract

## Information availability

- Before acting, read `docs/overview.md` for a high-level understanding of the
    project.
- Before running commands, read `docs/commands.md` for a description of
    available project commands and their usage.

## Prescriptive guidance

- Do not add persistent PlatformIO environments as a maintainability mechanism.
    The active project environments are `master`, `media`, `io`, `gpu0`, and
    `gpu1`; future persistent environments are expected only for `wheel` and
    `test`. Any other environment is a temporary agent-local diagnostic tool and
    must be removed before finishing unless the owner explicitly asks to keep
    it.
- Use `magic_enum` functionalities for cleaner semantic code when dealing with
    enums. Prefer this over manual string conversions, Count members and similar
    patterns.
- For LED animations and similar patterns, document non-obvious command
    parameters. E.g. `duration` is obvious, but `spokeGain` is not.
- **DO NOT** preserve backwards compatibility with previous versions of the
    project. The project is in active development, and there are no external
    dependencies on this project. Breaking changes are expected and acceptable
    if they serve to improve the project.

## Building and uploading

- Use `bin/build` to build the project if possible.
- Build output is rendered in SARIF format in `build/<env>/`. If a build fails,
    inspect the SARIF output; use `jq` or similar to reduce the amount of
    information to what is relevant. If you invoked `pio` directly, the SARIF is
    in the repository root.
- You may connect to the serial monitor using multi-monitor as documented at any
    time, and you may use it to reflash the environments as needed.

## Debugging and optimization

- Temporarily increase logging and metrics collection levels as needed and add
    trace-level instrumentation if helpful for debugging or optimization. Ensure
    that all statements added for debugging or optimization purposes are gated
    behind their respective compile-time constants; many macros already
    implement this, but for custom statements, you may need to add your own
    gates.
