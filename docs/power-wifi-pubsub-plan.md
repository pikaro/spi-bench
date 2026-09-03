# Power WiFi PubSub Bridge Plan

## Goal

Move WiFi and the UDP PubSub edge from the timing-critical Master ESP32-S3 to
the Power ESP32-C3. Power remains an SPI slave and bridges PubSub frames between
its UDP and SPI transports; Master remains the physical SPI bus owner and the
router for the other hardware links.

## Runtime configuration

- Power retains both credential records at all times:
  - station SSID `dre-guest`, with password secret `wifi-sta-pass`
  - access-point SSID `totem`, with password secret `wifi-ap-pass`
- `wifiConfig.mode` alone selects `Station` or `AccessPoint`; combined AP+STA
  operation is not introduced.
- Both password values remain independently stored in the Power node's secret
  NVS. Firmware configuration contains only their secret names; startup reads
  the secret selected by `wifiConfig.mode`.
- The initial mode is `AccessPoint` with one client, matching the existing
  constrained WiFi/lwIP pool sizing.
- UDP PubSub remains a single-learned-peer transport on port 2026. This is a
  PubSub gateway, not an IP bridge or NAT router.

## Implementation

- Give Power distinct SPI and UDP PubSub transport IDs.
- Expose the PubSub node owned by the SPI edge setup so Power can register the
  UDP transport on the same node.
- Start the SPI slave and PubSub node before starting WiFi, keeping an SPI DMA
  transaction available as early as possible.
- Start WiFi, register WiFi/network diagnostics, then start and register UDP
  PubSub against the Power node.
- Enable WiFi components for `env:power`, move the constrained WiFi/lwIP SDK
  settings from the Master overlay to a Power overlay, and make the shared UDP
  task affinity valid on the single-core ESP32-C3.
- Remove the obsolete commented WiFi/UDP implementation and configuration from
  Master.
- Do not change PubSub envelopes, topic IDs, payloads, or SPI wire formats.

## Validation

The Power hardware is temporarily installed as the Scratch node for battery
calibration. This change is therefore validated by compilation only for now;
there will be no upload, reset, monitor, or other on-device action.

- [x] Compile `env:power` with WiFi, lwIP, UDP PubSub, SPI, I2C, and the battery
  monitor enabled.
- [x] Compile `env:master` after removing its WiFi/network ownership.
- [x] Compile the remaining active PubSub environments (`media`, `gpu0`,
  `gpu1`, and `io`) because the transport traits and edge setup are shared.
- [x] Review the scoped migration diff. The migration did not invoke the wire
  generator or modify generated wire artifacts.

Deferred hardware checks after Power is available again:

- Provision both `wifi-sta-pass` and `wifi-ap-pass` on the Power node; do not
  copy credential values into the repository.
- Connect a host to the Power AP and establish the UDP peer.
- Verify host subscriptions and publications traverse Power SPI in both
  directions, including after independent Power and Master resets.
- Stress UDP traffic while monitoring SPI no-slot, CRC, sequence, timeout, and
  PubSub drop counters, plus minimum free heap and I2C sample failures.
- If WiFi scheduling starves the single queued SPI transaction, measure before
  changing priorities or adding double-buffered slave transactions.
