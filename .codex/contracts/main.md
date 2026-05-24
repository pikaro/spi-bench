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
