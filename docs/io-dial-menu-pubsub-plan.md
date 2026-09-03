# IO dial and menu PubSub event plan

Status: **event emission, brightness consumption, radial UI, and Battery selection implemented**
Last updated: **2026-09-01**

## Goal

Replace the IO node's provisional rotary diagnostic logs with typed PubSub
events produced by the existing local input and behavior stack.

The IO node remains authoritative for:

- GPIO decoding;
- rotary position changes;
- switch debounce and held-menu state;
- routing rotation between the ordinary dial and the held-button menu;
- current dial and menu positions;
- mapping a menu position to an application menu item.

The original phase published those outcomes without consuming them on master.
After the logic-board-v2 migration, master now consumes `PeripheralDial::Main`
for GPU brightness and its UI gauge. It also consumes `Selected / Battery` to
show the power node's latest fresh state of charge. A later follow-up connected
the remaining menu lifecycle events to the radial UI renderer.

## Architectural boundary

The event path must preserve the existing component-to-behavior-to-application
shape:

```text
GPIO2/GPIO3                         GPIO8
     |                                |
     v                                v
RotaryEncoder                    Button::Button
     | direction                     | Pressed/Released
     +---------------+----------------+
                     v
                 ButtonMenu
     | menu inactive                   | menu active
     v                                 v
Dial::onRotation()                menu lifecycle snapshot
     |                                 |
     v                                 v
Data::DialEvent                  Data::MenuEvent
     |                                 |
     +---------------+-----------------+
                     v
             PubSubEventProducer
                     |
                     v
                  PubSub
```

No raw rotary or switch state is reconstructed on another node. `ButtonMenu`
continues to run locally on IO, and its lifecycle events are the only
application-level meaning assigned to the GPIO8 switch.

## Scope

- Add generic bounded-dial behavior and event data.
- Extend `ButtonMenu` to report menu show, movement, and selection snapshots.
- Add application identities and wire payloads for the primary dial and main
  menu.
- Add dedicated `Dial` and `Menu` PubSub topics.
- Publish dial and menu events from `env:io` through the existing
  `PubSubEventProducer`.
- Generate wire metadata and validate every active firmware environment
  affected by the shared schema.
- Remove the provisional rotary log queue after typed publication is working.

## Original event-emission phase non-goals

- Do not add master subscriptions, queues, orchestration, or action handling.
- Do not implement the radial-menu LED animation or a GPU UI layer.
- Do not map dial value to display brightness on IO or master yet.
- Do not execute Reset, Calibrate, Toggle, Debug, or Battery locally on IO.
- Do not turn menu items or dial movements into synthetic buttons.
- Do not change bell or calibration-button behavior.
- Do not use `env:scratch` as a target or source of application policy.

## Shared application identities

Extend `include/Data/Peripherals.hpp` with identities rather than encoding
application meaning in the event type:

```cpp
enum class PeripheralDial : uint8_t {
    Main,
};

enum class PeripheralMenu : uint8_t {
    Main,
};
```

The encoder's integral switch is not a `PeripheralButton`: it is the local
held-state input to `ButtonMenu`. Existing `PeripheralButton::Bell` and
`PeripheralButton::Calibration` identities remain unchanged.

## Dial behavior and wire data

### Reusable behavior

Add an allocation-free `RotaryEncoder::Behavior::Dial` which owns:

- a dedicated `PositionConfig`;
- an atomic current position initialized from that config;
- scaling from the configured closed interval to `uint8_t`;
- one bounded inline event callback.

`Dial::onRotation(Direction)` advances its own position with
`PositionConfig::advance()`. An accepted increment invokes the callback with:

```cpp
struct Event {
    int32_t position;
    Direction direction;
    uint8_t value;
};
```

An increment rejected at either configured bound produces no event. The
callback may run in GPIO ISR context and must remain bounded, allocation-free,
and logging-free.

The dial configuration must contain both bounds and must satisfy
`minimum < maximum`; the existing `PositionConfig` permits optional and equal
bounds, but those cases cannot define a normalized value. Scaling uses checked
integer arithmetic and round-to-nearest:

```text
value = ((position - minimum) * 255 + (maximum - minimum) / 2)
        / (maximum - minimum)
```

The calculation must widen before subtraction and multiplication so the full
signed 32-bit position range is safe. Endpoints are exact:

```text
minimum -> 0
maximum -> 255
```

The IO brightness dial uses the settled policy `minimum = 0`, `maximum = 31`,
and `initialValue = 16`. This policy does not change the wire schema; consumers
still receive the normalized `value` in `[0, 255]`.

### Application wire payload

Add `include/Data/DialEvent.hpp`:

```cpp
struct WIRE_MSG DialEvent {
    int32_t position;
    Totem::RotaryEncoder::Direction event;
    PeripheralDial dial;
    uint8_t value;
};
```

The field order deliberately keeps the in-memory event at eight bytes on the
supported targets, allowing the complete immutable snapshot to pass through
the producer queue. Add size and trivially-copyable assertions.

The payload semantics are:

- `event`: direction of the accepted detent;
- `dial`: application identity attached by IO;
- `position`: accepted bounded dial position after the detent;
- `value`: the same position normalized to `[0, 255]` by the local config.

The consumer must use `value` for scale-independent control and may use
`position` for exact steps, diagnostics, or presentation.

## Menu behavior and wire data

### Generic `ButtonMenu` lifecycle output

Retain the existing ordinary-rotation callback, but replace the selection-only
menu callback with a generic lifecycle event:

```cpp
enum class ButtonMenuEventType : uint8_t {
    Shown,
    MovedClockwise,
    MovedCounterclockwise,
    Selected,
};

struct ButtonMenuEvent {
    int32_t position;
    ButtonMenuEventType event;
};
```

`ButtonMenu` emits:

- `Shown` after `Pressed` establishes the configured initial position;
- `MovedClockwise` or `MovedCounterclockwise` after an accepted held rotation,
  carrying the new absolute position;
- `Selected` after `Released`, carrying the final absolute position.

Rejected movement at a menu bound emits nothing. All events are snapshots;
future consumers must not need to reconstruct the selected position by
accumulating movement directions.

The menu callback can run in task context for show/select and ISR context for
movement, so it has the same bounded callback requirements as the dial.

### Main menu positions

Configure the main menu with:

```cpp
PositionConfig{
    .initialValue = 0,
    .minimum = -3,
    .maximum = 4,
};
```

Use a signed `int8_t` wire enum whose settled numeric values are the supplied
menu positions:

```cpp
enum class MenuItem : int8_t {
    None = -128,
    Reset = -3,
    Next = -1,
    Toggle = 0,
    Calibrate = 1,
    Debug = 2,
    Battery = 3,
};
```

The complete eight-position menu is:

| Position | Item |
| ---: | --- |
| -3 | `Reset` |
| -2 | `None` |
| -1 | `Next` |
| 0 | `Toggle` |
| 1 | `Calibrate` |
| 2 | `Debug` |
| 3 | `Battery` |
| 4 | `None` |

The two empty positions are deliberate. They remain visible in the absolute
position stream and preserve the geometric slots around the six menu
items. Releasing at an empty position still emits `Selected` with
`MenuItem::None`; IO does not invent an action or silently move to another item.

Store the eight-slot mapping as `constexpr` application data indexed by
`position - minimum`. Add compile-time assertions for the enum values, bounds,
slot count, and mapping.

### Application wire payload

Add `include/Data/MenuEvent.hpp`:

```cpp
enum class MenuEventType : uint8_t {
    Shown,
    MovedClockwise,
    MovedCounterclockwise,
    Selected,
};

struct WIRE_MSG MenuEvent {
    int32_t position;
    MenuEventType event;
    MenuItem item;
    PeripheralMenu menu;
};
```

The application adapter translates the generic `ButtonMenuEventType` to the
wire `MenuEventType`, looks up the item for its absolute position, and attaches
`PeripheralMenu::Main`. Keeping behavior types out of the wire payload permits
the generic behavior to evolve independently of application identities while
retaining identical event semantics.

Arrange the fields so the in-memory payload is eight bytes, then assert its
size and trivial copyability. The generated encoded payload is smaller than
the PubSub 64-byte limit.

## Rotary switch ownership

The rotary switch's debounced `Pressed` and `Released` transitions feed
`ButtonMenu::onButton()` directly and remain local to IO. Do not publish them as
`Data::ButtonEvent`, and do not attach a `PressClassifier` to GPIO8. A
post-release `Press`, delayed single press, double press, or long press would be
a second application interpretation competing with the radial menu's
`Shown`/`Selected` lifecycle.

This does not change the existing button model for independent physical
buttons such as Bell and Calibration. It only establishes that the encoder
switch is part of the composite menu control rather than a standalone button.

## PubSub topics and generated wire metadata

Append two topic flags without renumbering existing topics:

```cpp
Dial = 1U << 20,
Menu = 1U << 21,
```

The topic-to-payload contract is one type per topic:

| Topic | Payload | Publisher in this phase | Consumer in this phase |
| --- | --- | --- | --- |
| `Button` | `Data::ButtonEvent` | IO's independent buttons | existing consumers only |
| `Dial` | `Data::DialEvent` | IO | none |
| `Menu` | `Data::MenuEvent` | IO | none |

Export the new application headers from `include/Data/Facade.hpp`. Run
`make wire PIO_ENV=io` after adding the `WIRE_MSG` structs. Do not hand-edit
`include/Generated/Wire/`; the generator must create the two field-list headers
and update `Generated/Wire/All.hpp`.

## Feeding events from IO

### Producer queue capacity

Both application payload objects are designed to fit in eight bytes. Increase
`PubSubEventProducerConfig::maxArgumentSize` from four to eight and keep the
factory capture limit unchanged. The increase adds a fixed, small amount to
each statically allocated producer request and permits the complete snapshot to
be copied into the ISR-safe queue without pointers or shared mutable staging.

Use stateless identity factories:

```cpp
eventProducer.makeCallback<Totem::Data::DialEvent>(
    PubSubService::Topic::Dial,
    [](Totem::Data::DialEvent event) { return event; });

eventProducer.makeCallback<Totem::Data::MenuEvent>(
    PubSubService::Topic::Menu,
    [](Totem::Data::MenuEvent event) { return event; });
```

The existing producer default `requireSyncedClock = false` is correct for local
human input. Events may be emitted before clock synchronization just like the
current button events.

Do not increase the producer queue depth speculatively. Exercise rapid dial and
menu rotation on hardware and use the existing producer queue-drop metrics to
decide whether depth eight is insufficient.

### Local composition

Construct the local behaviors in this order:

1. Create the dial PubSub callback.
2. Construct `Dial` with that callback and `dialPositionConfig`.
3. Create the menu PubSub callback which maps the generic menu event to the
   main-menu item table.
4. Construct `ButtonMenu` with ordinary rotation forwarded to
   `Dial::onRotation()`, the menu callback, and `menuPositionConfig`.
5. Construct `RotaryEncoder` with rotation forwarded to
   `ButtonMenu::onRotation()`.
6. Construct the rotary switch with its callback forwarded only to
   `ButtonMenu::onButton()`.

Keep every object statically allocated. Reorder declarations around the
existing `eventProducer` as needed so callback captures never reference an
object before construction.

The physical `RotaryEncoder::position()` must not be used for dial events. Its
counter advances before every callback, including rotations subsequently
claimed by the menu. `Dial` owns the ordinary-mode position, and menu rotations
never reach it.

### Lifecycle and work loop

Preserve the existing setup ordering:

- configure the PubSub network before beginning `eventProducer`;
- begin `eventProducer` before beginning any input that can invoke a publishing
  callback;
- begin the rotary encoder and switch after their callbacks and local behaviors
  are fully constructed.

In the IO loop:

- continue servicing `RotaryEncoder::work(nowMs)` and
  `rotarySwitch.work(nowMs)`;
- remove `workRotationLogs()` and its dedicated queue after typed publication
  replaces the provisional diagnostic path.

The `Dial` and `ButtonMenu` behaviors own no tasks and require no `work()`
method beyond the classifier timeout work already described.

## Implementation phases

### Phase 1 - Pure behavior and application types

- [x] Add the bounded `Dial` behavior and integer normalization helper.
- [x] Extend `ButtonMenu` with show/move/select lifecycle snapshots.
- [x] Add dial, menu, item, and peripheral application types.
- [x] Add `DialEvent` and `MenuEvent` wire payloads and facade exports.
- [x] Add the two topic flags without changing existing values.

### Phase 2 - IO composition and publication

- [x] Add main-menu bounds and the eight-position item table.
- [x] Select and document brightness bounds `0..31` and initial position `16`.
- [x] Increase the producer input-copy bound to eight bytes.
- [x] Replace the rotary log callbacks with Dial and MenuEvent publisher
  callbacks while keeping the switch local to ButtonMenu.
- [x] Remove the provisional log queue.

### Phase 3 - Wire generation and validation

- [x] Run `make wire PIO_ENV=io` and inspect generated field order.
- [x] Add host logic tests for scaling, bounds, routing, lifecycle order, and
  sparse menu mapping.
- [ ] Build all active environments because the topic enum, producer request,
  Data facade, and generated wire registry are shared. `master`, `io`, `gpu0`,
  and `gpu1` pass; `media` is currently blocked by its unrelated ESP32-S3 pin
  configuration and audio-tools compile errors.
- [ ] Flash IO and confirm event-producer publication with zero queue drops
  during ordinary and deliberately rapid interaction.
- [ ] Confirm GPIO8 produces only menu lifecycle events, without rotary
  `ButtonEvent` traffic.
- [x] Update `docs/overview.md` when implementation changes the documented IO
  behavior.

## Validation plan

Add a small host test following the existing `test/*.cpp` and
`bin/test-*` pattern. Cover at least:

- normalization at minimum, midpoint, maximum, and asymmetric signed ranges;
- wide intermediate arithmetic without signed overflow;
- rejection of missing, equal, or reversed dial bounds;
- accepted clockwise/counterclockwise increments and no event at bounds;
- ordinary rotation reaching Dial only while the menu is inactive;
- `Shown -> Moved* -> Selected` menu event order and absolute positions;
- held menu rotation not changing the dial position;
- exact item values and `None` at position `-2`;
- selection of an empty slot remaining `MenuItem::None`;
- trivial-copy and producer-size assertions for both wire payloads.

Then run:

```text
bin/test-rotary-input
make wire PIO_ENV=io
bin/build -e master -e media -e io -e gpu0 -e gpu1
```

On attached IO hardware, exercise:

1. ordinary clockwise and counterclockwise rotation across the configured dial
   range;
2. repeated turns against both dial bounds;
3. press, held rotation through every menu position, and release;
4. selection at both empty positions;
5. repeated press/release cycles without rotary `ButtonEvent` traffic;
6. rapid reversal and fast rotation while checking `evtCore`, `events`, and
   `evtDiag` queue/publication counters.

No master-side behavior is required for this validation. If payload inspection
is needed before the master migration, use a temporary diagnostic subscriber
or trace tool during bench testing and remove it before completing the phase.

## Master and GPU follow-up

The follow-up now:

- subscribing to `Dial` and `Menu`;
- mapping `PeripheralDial::Main` and its normalized value to display
  brightness;
- translating `Shown` and `Moved*` snapshots into radial-menu play commands;
- stopping the radial-menu presentation on `Selected`;
- handling the Battery selection with the latest usable power estimate;
- handling Next by starting the existing fade to the next FFT/background visual;
- treating `MenuItem::None` as a deliberate no-op;
- adding a dedicated GPU UI layer and renderer.

Reset, Toggle, and Calibrate selection actions remain unimplemented. Next starts
the existing background-visual fade, Debug toggles the master-owned debug mode,
and Battery retains its gauge action.

Those consumers must use the absolute `position`, `item`, and normalized dial
`value` supplied by IO rather than reconstructing state from event history.
