# Bluetooth Wheel Port Plan

This is a planning note for porting the old wheel BLE client from
`../led/upper/include/Bluetooth` into this ESP-IDF-based repository. The
current target is the `io` node, but the component shape should allow any ESP
node with Bluetooth support to opt in later.

## Scope

- Do not change `../led/wheel` for the first port. It works well enough and is
  still the source of truth for the peripheral protocol.
- Use ESP-IDF NimBLE directly. Do not introduce ArduinoBLE or NimBLE-Arduino
  into this repository.
- Keep project-owned state static or fixed-capacity. The NimBLE host task and
  controller internals are ESP-IDF-owned system tasks/storage and are outside
  the repo-owned static task abstraction.
- Publish the wheel value through PubSub as a compact typed event. The existing
  `Totem::Wheel::WheelState` is the right first PDU.
- Keep the Bluetooth component generic from the first implementation. Generic
  config defaults must not mention this repository's nodes or the wheel device;
  node-specific adjustments belong in `src/io/config.hpp`.
- Use `include/StaticConfig/` only for values that must be global component
  `constexpr` sizing or policy. Node-specific `constexpr` choices are still
  node config, not static component defaults.

`Topic::Wheel` is the dedicated PubSub topic for this input. Do not reuse a
generic sensor topic because PubSub subscribers currently decode a single
expected payload type per topic.

## Implementation Status

The first port is implemented and running on `env:io`.

- `include/Bluetooth/` now provides a generic fixed-capacity BLE central
  component with an ESP-IDF NimBLE backend and a null backend for targets
  without enabled NimBLE support.
- `include/Wheel/` now provides `BleWheel`, a concrete BLE device-profile
  driver for the nRF wheel protocol.
- `src/io/config.hpp` binds the `io` node to the wheel driver and keeps
  node-specific BLE scan tuning out of generic defaults.
- `src/io/main.cpp` starts `BleWheel` after PubSub is configured and starts the
  Bluetooth central on the `io` node.
- `sdkconfig.stack.io` enables the NimBLE pieces required by this ESP-IDF
  package.

Validation on 2026-05-24:

- `bin/build -e io` succeeds.
- `pio run -e io -t upload` succeeds after a physical reset of the ESP32-C3
  board when the USB Serial/JTAG port previously stopped entering the
  bootloader.
- Runtime logs show scan start, wheel advertisement match, connection, service
  discovery, characteristic discovery, CCCD write, and subscription.
- `/metrics` on `io` showed `ble notif=270 drop=0 fail=0` and
  `wheel notif=270 publish=270 bad=0 fail=0`.

Weak-link resilience update on 2026-09-01:

- The wheel's pinned ArduinoBLE 2.0.2 dependency is patched at build time to
  bound HCI ACL-buffer backpressure to one second. A failed notification now
  returns to wheel firmware so it can log the failure and disconnect instead
  of blocking the main loop indefinitely. The wheel-only build hook verifies
  the exact dependency revision and refuses to patch an unexpected or dirty
  checkout.
- The wheel logs subscribe/unsubscribe, notification failure or latency,
  advertising retry failure, invalid IMU FIFO reads, and bounded FIFO-drain
  yields. It also reports the FIFO overrun bit correctly. Per-sample gyro
  anomalies remain filtered before integration, while accumulated valid
  movement is no longer incorrectly rejected by the per-sample threshold.
- The `io` central logs connection parameters, connection and PHY update
  failures, termination failures, notification counts and silence at
  disconnect, and notification delivery resuming after at least ten seconds.
- `pio run -e main` in `../led/wheel` and `bin/build -e io` both succeed with
  these changes.

## Existing Behavior

The old central side is split across:

- `../led/upper/include/Bluetooth/BleCentral.hh`
- `../led/upper/include/Bluetooth/BleScanner.hh`
- `../led/upper/include/Bluetooth/BleConnectionHandler.hh`
- `../led/upper/include/Wheel/WheelDriver.hh`
- `../led/upper/include/Common/Data/BluetoothData.hh`

The wheel peripheral side is:

- `../led/wheel/include/Bluetooth/BLEPeripheral.hh`
- `../led/wheel/src/main.cpp`
- `../led/wheel/include/Wheel/Wheel.hh`

Protocol constants from the old code:

```text
service name: totem
service UUID: DE516A65-57B9-471C-ACC3-4F295834594C
angle UUID:   B9E414E0-388D-4A46-9146-01D4449FC2A2
command UUID: 740E221B-77EF-4515-BBD3-2B860CF0684D
```

The command characteristic UUID is configured in the wheel firmware but is not
created by `BLEPeripheral`; it should not be part of the first port.

Runtime behavior to preserve:

- The wheel advertises the service UUID and the local name `totem`.
- The old central matches advertisements by service UUID and connectability,
  not by MAC address.
- The central subscribes to the angle characteristic with notifications enabled
  and indications disabled.
- Each notification payload is exactly one 4-byte native little-endian `float`.
- The wheel only writes the angle after `hasMovedSignificantly()` returns true.
  The default threshold is 1.0 degree.
- The wheel angle is normalized to roughly `[-180, 180)`.

## Issues To Avoid

Do not copy these old central-side properties into the new component:

- It uses `std::string`, `std::function`, `std::vector`, `std::unordered_map`,
  `std::unordered_set`, `std::unique_ptr`, and heap-created driver objects.
  The new component should use fixed arrays, static driver instances, and
  bounded queues.
- `BleScanner::onScanEnd()` always restarts scanning. That is fragile because
  stopping a scan to connect can also produce a scan-end callback. The new
  state machine should only restart scanning from explicit idle, failed,
  disconnected, or timed-out states.
- `BleScanner::onDisconnect()` removes the active driver but does not remove
  the matching session from `BleConnectionHandler::_activeSessions`, so the old
  active-session count can become stale after disconnect.

Wheel-side resilience finding:

- The wheel does not send the current angle immediately after a new central
  subscribes. After reconnect, consumers keep their last value until the wheel
  moves more than the significant-movement threshold. That is not fatal for the
  first port, but it is the one wheel firmware behavior worth fixing later if
  reconnect freshness matters.

## Current Repo Hooks

Relevant current files:

- `src/io/main.cpp` owns the `io` node setup.
- `src/io/config.hpp` owns `io` static config values.
- `include/Wheel/Interfaces/Wire.hpp` already defines:

```cpp
struct WIRE_MSG WheelState {
    Angle<uint16_t> position;
    Angle<uint16_t> delta;
};
```

- `include/Types/Angle.hpp` maps an unsigned integer to a modular angle.
- `include/PubSubBackend/detail/Codec.hpp` treats aggregate wrappers with a
  public `value` member as transparent wire values, so `Angle<uint16_t>`
  already serializes as one little-endian `uint16_t`.
- `include/Generated/Wire/Totem__Wheel__WheelState.hpp` is already generated.
- `include/StaticConfig/Stacks.hpp` is where repo-owned task stack constants
  and static task storage specializations belong.

The `io` setup order should become:

1. `core.beginStatusLedEarly()`
2. `core.setup()`
3. `rs485Slave.begin()`
4. local peripherals such as `ledPwm`
5. `pubSubNetwork.setup()`
6. local PubSub subscribers
7. wheel publisher and Bluetooth central
8. shared PubSub event producer
9. independent button instances

The wheel publisher can create envelopes with `requireSyncedClock = false`,
matching buttons. This keeps early wheel events from being dropped before clock
sync; the PubSub timestamp is zero until a synced clock exists.

## Component Shape

Add a top-level Bluetooth component:

```text
include/Bluetooth/Facade.hpp
include/Bluetooth/Interfaces/Config.hpp
include/Bluetooth/Interfaces/Device.hpp
include/Bluetooth/Interfaces/Types.hpp
include/Bluetooth/detail/Central.hpp
include/Bluetooth/detail/Metrics.hpp
include/Bluetooth/detail/PlatformSelect.hpp
include/Bluetooth/detail/platform/NimbleCentral.hpp
include/StaticConfig/Bluetooth.hpp
```

The public facade should expose only `Bluetooth::Central`, config/types, and
the device-driver interface. ESP-IDF NimBLE headers should stay in
`detail/platform`.

`Bluetooth::Config` defaults should describe generic BLE-central behavior only.
They may contain conservative generic scan/task defaults, but must not contain
wheel UUIDs, `io` node assumptions, PubSub topics, or project-specific device
selection. `src/io/config.hpp` should construct the actual `io` central config
from those defaults and attach the wheel driver there.

`include/StaticConfig/Bluetooth.hpp` should be limited to hard component-wide
compile-time sizing such as maximum driver slots, subscription slots, queued
notification count, and copied notification payload bytes. Do not put
node-specific scan cadence, expected peer address, or active device selection
there.

Use a static driver model:

- `Bluetooth::IDeviceDriver` exposes `matches(advert)`,
  `onConnected(session)`, `onDisconnected(peer, reason)`, and
  `onNotification(notification)`.
- `Bluetooth::Config` owns a fixed driver pointer array sized from
  `StaticConfig::Bluetooth::maxDrivers`.
- Driver objects are normal globals in the owning `src/<node>/main.cpp` or in a
  node-local setup object. The registry only stores non-owning pointers.
- No driver creation through `new`, `unique_ptr`, or factories is needed for
  the first port.

The central owns:

- one active ESP-IDF NimBLE host instance
- one active connection at first, matching the current wheel use case
- fixed subscription slots mapping `(conn_handle, attr_handle)` to a driver
- a bounded notification queue copied from NimBLE callbacks
- a project-owned task that drains queued notifications and invokes drivers

NimBLE callbacks run on the NimBLE host task. They should not publish PubSub
events directly. Copy the small notification payload into the central queue and
wake the project-owned task; let the driver publish from that task.

## NimBLE Flow

Use the ESP-IDF NimBLE C API available in the local PlatformIO ESP-IDF package.
Important calls and headers:

- `nimble_port_init()`
- `nimble_port_freertos_init()`
- `ble_hs_cfg.reset_cb`
- `ble_hs_cfg.sync_cb`
- `ble_store_config_init()`
- `ble_hs_util_ensure_addr(0)`
- `ble_uuid_from_str()`
- `ble_gap_disc()`
- `ble_gap_disc_cancel()`
- `ble_gap_connect()`
- `ble_gattc_disc_svc_by_uuid()`
- `ble_gattc_disc_chrs_by_uuid()`
- `ble_gattc_disc_all_dscs()`
- `ble_gattc_write_flat()` for the CCCD notification value `{1, 0}`
- `os_mbuf_copydata()` for notification payloads

Startup:

1. Validate config and install a single active central instance.
2. Initialize metrics and the notification queue.
3. Initialize NVS idempotently before NimBLE, because ESP-IDF NimBLE examples
   do this for controller/PHY storage and core setup does not currently do it.
4. Call `nimble_port_init()`.
5. Set `ble_hs_cfg.reset_cb` and `ble_hs_cfg.sync_cb`.
6. Call `ble_store_config_init()` unless the final Kconfig disables all store
   users cleanly.
7. Start the NimBLE host task with `nimble_port_freertos_init()`.
8. On sync, call `ble_hs_util_ensure_addr(0)` and start scanning.

Scanning:

- Use active scanning to match the old NimBLE-Arduino behavior.
- Generic defaults use BLE units 160/80. The initial `io` wheel config widens
  the window to 160/160, matching the old "always listening while scanning"
  behavior without putting node-specific tuning in generic defaults.
- Use a 5000 ms scan duration and restart when discovery completes while idle.
- Parse advertisement fields with `ble_hs_adv_parse_fields()`.
- Match connectable advertisements that include the configured wheel service
  UUID in `fields.uuids128`.
- Stop scanning before connecting. Track an internal `Connecting` state so the
  expected discovery-complete event does not immediately restart scanning.

Connection and subscription:

1. Connect with the configured timeout, initially 5000 ms.
2. On successful connect, call the matched driver's `onConnected(session)`.
3. The wheel driver discovers the service by UUID.
4. It discovers the angle characteristic by UUID within the service handles.
5. It discovers the CCCD descriptor for the angle characteristic.
6. It writes `{1, 0}` to the CCCD to enable notifications.
7. It registers the characteristic value handle in the central subscription
   table.
8. On notification, the central copies the payload into its queue and wakes the
   project task.
9. On disconnect, connect failure, discovery failure, subscribe failure, or
   host reset, clear handles, notify the driver, and resume scanning.

## Wheel Driver

Add a wheel component facade and driver around the existing wire PDU:

```text
include/Wheel/Facade.hpp
include/Wheel/Interfaces/Config.hpp
include/Wheel/Interfaces/Wire.hpp
include/Wheel/detail/BleWheel.hpp
include/Wheel/detail/Metrics.hpp
```

`BleWheel` should implement `Bluetooth::IDeviceDriver` and own:

- service UUID string
- angle characteristic UUID string
- optional expected peer address, disabled by default to preserve old matching
- last known `Angle<uint16_t>`
- a `PubSubBackend::Pool<WheelState, N>` for queued publishes
- fixed handles for the active connection, service, characteristic, and CCCD

Unlike the generic Bluetooth component, `BleWheel` represents one concrete BLE
device profile. Its defaults may include the wheel service and characteristic
UUIDs, the same way a driver for a commercial BLE sensor would know that
sensor's GATT profile. The `io` node should still instantiate and tune the
driver from `src/io/config.hpp`.

Notification handling:

1. Require `payload.size == sizeof(float)`.
2. Copy bytes into a `float` with `std::memcpy`.
3. Reject NaN and infinity.
4. Convert to `Angle<uint16_t>` with `Angle<uint16_t>::fromDeg(angleDeg)`.
5. Compute `delta = current - previous`; use zero delta for the first sample.
6. Publish `WheelState{.position = current, .delta = delta}`.

`Angle<uint16_t>::deg()` returns `[0, 360)`. Consumers that need the old signed
domain should convert values greater than or equal to 180 degrees by subtracting
360.

## PubSub Integration

Publish `WheelState` on `NodeData::PubSub::Topic::Wheel`. The topic already
exists in `include/Data/PubSub.hpp`.

The wheel publishes directly from Bluetooth task context and should retain the
fixed-pool pattern:

- store the event in a fixed `PubSubBackend::Pool`
- create `Envelope::make<WheelState>()`
- release the pool slot on envelope creation or publish failure
- use `PubSubService::get().nodeId()` as the source
- increment metrics for notifications, malformed payloads, publish failures,
  and published states

## Build Configuration

Enable BLE only for `io` initially.

In `platformio.ini`, extend `env:io` without losing inherited CMake args:

```ini
board_build.cmake_extra_args =
    ${esp32c3supermini.board_build.cmake_extra_args}
    -DSRC_ROOT:STRING="io"
    -DENABLE_LEDC=ON
    -DENABLE_BLUETOOTH=ON
```

Add `sdkconfig.stack.io` with a small central-only NimBLE profile. Start with:

```text
CONFIG_BT_ENABLED=y
CONFIG_BT_CONTROLLER_ENABLED=y
CONFIG_BT_BLE_ENABLED=y
CONFIG_BT_CLASSIC_ENABLED=n
CONFIG_BTDM_CTRL_MODE_BLE_ONLY=y
CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY=n
CONFIG_BTDM_CTRL_MODE_BTDM=n
CONFIG_BT_BLUEDROID_ENABLED=n
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_ROLE_CENTRAL=y
CONFIG_BT_NIMBLE_ROLE_OBSERVER=y
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y
CONFIG_BT_NIMBLE_ROLE_BROADCASTER=n
CONFIG_BT_NIMBLE_GATT_SERVER=y
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
CONFIG_BT_NIMBLE_MAX_BONDS=1
CONFIG_BT_NIMBLE_MAX_CCCDS=1
CONFIG_BT_NIMBLE_GATT_MAX_PROCS=2
CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=23
CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT=6
CONFIG_BT_NIMBLE_MSYS_2_BLOCK_COUNT=12
CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=4096

CONFIG_LWIP_ENABLE=y
CONFIG_ESP_NETIF_TCPIP_LWIP=y
```

`CONFIG_BT_NIMBLE_ROLE_PERIPHERAL` and `CONFIG_BT_NIMBLE_GATT_SERVER` are not
used by the runtime flow, but this ESP-IDF build needs them enabled so NimBLE
deinit links cleanly. LwIP is enabled because ESP-IDF Bluetooth headers pull in
Wi-Fi error headers that include LwIP headers in this package.

Do not add new project-owned build flags for UUIDs, pool sizes, scan windows,
or driver selection. Put ordinary runtime config defaults in the owning
component's `Interfaces/Config.hpp`, component-wide compile-time sizing in
`include/StaticConfig/`, and `io` adjustments in `src/io/config.hpp`.

## Verification

Code-level verification after implementation:

1. If `WheelState` fields change, run `make wire PIO_ENV=io`.
2. Build `io`: `bin/build -e io`.
3. If `Data::PubSub::Topic` changes, build all active PubSub environments:
   `master`, `media`, `gpu0`, `gpu1`, and `io`.
4. Check the `io` size output and task-stack report because enabling BLE on an
   ESP32-C3 4 MiB target may be tight.

Hardware verification:

1. Upload only the `io` node during this work.
2. Do not upload or reset the nRF wheel from this repo. The current
   `reset_target.py` path only gives a useful `reset-bootloader` flow for the
   serial nRF case, which is not helpful for runtime testing.
3. If ESP32-C3 upload, reset, or erase fails with `No serial data received`,
   the board may have stopped responding on USB Serial/JTAG. A physical reset
   cleared the observed failure; reset or erase can then be retried before
   uploading again.
4. Use multi-monitor for logs, for example:

```text
bin/monitor-multi --strip-ansi io
```

5. Expected `io` logs: NimBLE init, scan start, matching wheel advertisement,
   connect, service discovery, characteristic discovery, CCCD write, subscribed,
   and metrics showing notifications and wheel publishes.
6. Query `/metrics` on `io` and check the `ble` and `wheel` groups. Successful
   movement should increment both `ble.notif` and `wheel.publish` without
   `ble.drop`, `ble.fail`, `wheel.bad`, or `wheel.fail`.
7. Test reconnect by power-cycling the wheel or moving it out of range. Expected
   `io` behavior: disconnect, clear handles, restart scan, reconnect, resubscribe.
8. After reconnect, expect no fresh angle until the wheel moves by more than the
   current wheel threshold. That is existing wheel behavior.

## Later Work

- Decide whether to add a wheel command characteristic on the nRF side. The old
  UUID exists but no characteristic is created today.
- If reconnect freshness matters, update the wheel firmware to publish the
  current angle once a central subscribes.
- Add a small console diagnostic command on `io` to report BLE state, peer,
  last angle, notification count, and last error.
