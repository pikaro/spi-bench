# Monitor/upload validation notes

- `bin/monitor-multi --upload` with multiple environments is unreliable on the ESP32-C3/S3 stack because the concurrent PlatformIO/managed-component work can race. Prefer sequential uploads: `bin/build -e <env> -t upload` for each environment, then start `bin/monitor-multi --strip-ansi ...` for validation.
- Running `pio` directly may fail because the shell used by tools does not always have PlatformIO on `PATH`; use the repo wrappers (`bin/build`, `bin/monitor-multi`) unless intentionally debugging PlatformIO itself.
- `pio device monitor` may emit `Esp32ExceptionDecoder`/build noise when monitor filters initialize. Treat that as host-side monitor setup unless device logs show an actual panic/reset afterwards.
- In `monitor-multi`, send commands with `!<env> <command>`, e.g. `!gpu0 /metrics` or `!master /anim wheel-update 160 96 3 1 1`.