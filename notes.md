- Add Core enum analogous to Pin

=== ======

◐ Now I see something very important. In `sdkconfig.stack.master` (the shared
stack for all devices including slave!):\`\`\`
CONFIG_HEAP_ABORT_WHEN_ALLOCATION_FAILS=y CONFIG_ESP_SYSTEM_PANIC_GDBSTUB=y
CONFIG_ESP_SYSTEM_PANIC_PRINT_HALT=y

`CONFIG_ESP_SYSTEM_PANIC_GDBSTUB=y` and `CONFIG_ESP_SYSTEM_PANIC_PRINT_HALT=y`
are mutually exclusive Kconfig choices (they're in the same Kconfig choice
group). Setting both would be an issue, and which one "wins" depends on how the
sdkconfig assembler handles it.

`PRINT_HALT` would override `GDBSTUB` since it comes after, but this applies to
all platforms so it shouldn't explain the C3-specific failure. However,
`CONFIG_HEAP_ABORT_WHEN_ALLOCATION_FAILS=y` is the real concern—the ESP32C3 only
has 320KB of heap compared to the S3's 512KB plus PSRAM, so with all the stack
allocations, mutexes, and task stacks, there might not be enough memory on C3,
which would trigger an abort.

The key issue is that ESP32C3 doesn't actually support GDB stub, so if
`CONFIG_ESP_SYSTEM_PANIC_GDBSTUB=y` is set and wins in the configuration
ordering, it could fail on C3. I need to check the shared base configuration
files to see how these settings are assembled across platforms.

=== ===========

static constexpr ::platform::Tick defaultEndKillDelayMs = 100;

bullshit

=== =========

