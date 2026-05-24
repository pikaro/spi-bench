# WiFi Phase 1 Bring-Up

- Active Phase 1 WiFi is master-integrated and generic under `include/Wifi/`; `include/Network/` provides thin lwIP UDP/TCP diagnostic wrappers.
- AP and station modes are intentionally mutually exclusive for now. Combined AP+STA is deferred until there is a concrete channel/reconnect/IP policy.
- Master credentials live only in ignored `src/master/wifi_credentials.hpp`; tracked `src/master/wifi_credentials.example.hpp` defines separate `Station` and `AccessPoint` structs plus selected `Totem::Wifi::Mode`. `src/master/config.hpp` falls back to disabled WiFi when the ignored header is absent.
- NVS-backed credential storage is a later direction, not part of the first pass.
- Master registers `/wifi`, `/udp-send`, `/udp-recv`, `/tcp-connect`, and `/tcp-listen`. Network command timeouts are capped to avoid command task watchdog stalls.
- Host-side network diagnostics use `bin/net-probe` for UDP/TCP one-shot send/receive and `route-get`.
- Hardware validation: STA has connected to the guest network and obtained `192.168.179.5`; AP mode was temporarily selected and `/wifi` reported AP started. After the host joined the guest WiFi as `192.168.179.6/24`, TCP validates in both directions, UDP validates MCU-to-host, and UDP exchange validates host echo replies to MCU-originated flows including a bound MCU source port. Unsolicited host-to-MCU UDP still times out even after `/udp-recv` logs that its socket is listening, so Phase 2 should consider an MCU-originated UDP heartbeat/flow setup for networks with stateful station UDP behavior.