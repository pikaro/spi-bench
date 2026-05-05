# Monitoring printf/SV_ARG pitfall

`SV_FMT` expands to one `%*.*s` conversion and `SV_ARG(...)` expands to three printf arguments. In firmware log calls, an unmatched `SV_ARG` in a variadic `_log_*` call shifts every following argument at runtime. This caused `/monitor` managed-task rows on `env:media` (ESP32 original) to print bogus priority/core/stack/time values until the extra `SV_ARG(magic_enum::enum_name(task.platformState))` was removed from `include/Monitoring/detail/Commands.hpp`.
