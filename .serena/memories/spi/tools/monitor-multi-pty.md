# monitor-multi PTY requirement

PlatformIO's `pio device monitor` uses pyserial miniterm and calls `termios.tcgetattr()` on stdin during startup. In non-interactive Bash background jobs, stdin may be `/dev/null`, which causes `termios.error: (19, 'Operation not supported by device')` before serial monitoring starts.

`bin/monitor-multi` should give each owned child monitor a real pseudo-terminal. The current implementation uses Python `pty.fork()` to run `bin/monitor <env> [baud]` for environments it owns. It supports bounded and low-noise captures with `--timeout`, `--strip-ansi`, `--include`, `--exclude`, `--summary`, `--max-lines`, and `--log-file`. It also supports prefix-friendly `--reset` and `--upload` flags, which pass `RESET=true` / `UPLOAD=true` to owned `bin/monitor` subprocesses.

The first instance for an env owns the serial monitor and writes a per-env spool file under `/tmp`; later instances attach to the spool so multiple terminals can slice the same complete stream independently. Do not use one output FIFO for this because multiple FIFO readers split bytes. Interactive `!<env> <command>` sends directly to a locally owned PTY or through the per-env command FIFO to the owner process.
