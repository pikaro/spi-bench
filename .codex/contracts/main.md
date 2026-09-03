# Project contract

Extends General, the language contracts, Embedded, and the active phase contract.
Holds only what is specific to this repository.

**Active phase: Dev phase.**

______________________________________________________________________

## Information availability

- Before acting, read `docs/overview.md` for a high-level understanding of the
    project.
- Before running commands, read `docs/commands.md` for available project commands
    and their usage.

______________________________________________________________________

## Environments

- Active environments: `master`, `media`, `power`, `io`, `gpu0`, `gpu1`.
    Future persistent environments are expected only for `wheel` and `test`.
- Do not add persistent PlatformIO environments as a maintainability mechanism.
    Any environment beyond that list is a temporary agent-local diagnostic tool and
    must be removed before finishing unless the owner asks to keep it.
- These environments are the hardware roles governed by *Embedded → Startup and
    identity*. Which environments to rebuild is governed by *Embedded →
    Rebuilding*.

______________________________________________________________________

## Project dependencies

- Use `magic_enum` for semantic enum handling. Prefer it over manual string
    conversions, `Count` members, and similar patterns.

______________________________________________________________________

## Embedded logging

- Use the project logging facility and identify stable, searchable events in
    concise text.
- Bounded scalar formatting in `_log_*` calls is the native structured context
    mechanism for this project. Structured fields are required only when the
    selected backend supports them directly.
- Keep logging allocation-free, exclude secrets, and follow the embedded
    restrictions on ISRs, real-time output loops, and high-frequency progress.

______________________________________________________________________

## Locks and I/O

- By default, do not hold a state or coordination lock across blocking I/O,
    callbacks, unbounded loops, or unrelated work.
- A resource-owner lock may span an I/O transaction when it serializes that
    exact resource, correctness requires serialization, the operation has a
    configured bound, no unrelated state lock is nested, and no user callback
    runs while held.
- The I2C master mutex is such a resource lock. A BatteryMonitor state lock held
    across LittleFS access is not.

______________________________________________________________________

## Tracked platform caveats

- The ESP32 filesystem backend currently uses `std::FILE`. Its steady-state
    allocation behavior and open/append/flush/close latency have not yet been
    proven. Treat this as a measured follow-up, not as an exception to the
    embedded allocation rules.

______________________________________________________________________

## Building and uploading

- Use `bin/build` to build where possible.
- Build output is SARIF in `build/<env>/`. On failure, inspect the SARIF; use `jq`
    to reduce it to what is relevant. When `pio` was invoked directly, the SARIF is
    in the repository root.
- You may connect to the serial monitor using multi-monitor as documented at any
    time, and use it to reflash environments as needed.

______________________________________________________________________

## Domain documentation

- For LED animations and similar patterns, document non-obvious command
    parameters. `duration` is obvious; `spokeGain` is not.
