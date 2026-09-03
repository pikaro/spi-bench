# INA2xx and BatteryMonitor Contract Alignment Plan

Status: **software implemented; first hardware validation pending**
Last updated: **2026-08-26**

## Goal

Align the existing INA2xx and BatteryMonitor implementation with the expanded
C++ and embedded contracts without destabilizing the working INA226 prototype
or weakening the intended INA228 architecture.

The implementation should preserve the current component split:

- INA2xx owns I2C access, model-specific conversion, electrical limits, sample
  delivery, and hardware ALERT behavior.
- BatteryMonitor owns battery semantics, integration, runtime estimates,
  calibration, and durable profiles.

The main changes are to make BatteryMonitor ownership and persistence safe,
correct fixed-point power averaging and estimate validity, separate domain
states from operational failures, tighten public interfaces, and add the host
verification that these hardware-independent paths now require.

## Confirmed decisions

### INA228 is a protected target

INA228 is the intended production device. INA226 is the physically available
prototype before the next board fabrication and remains useful for future
projects. INA219 and INA226 support must remain reusable rather than becoming a
temporary compatibility layer.

Therefore:

- Retain the semantic temperature, energy, and charge capabilities needed by
  INA228.
- Retain a model-independent semantic sample boundary.
- Keep model-specific register widths, encodings, identity, accumulators, and
  ALERT behavior out of the common public API.
- Do not force INA228 through the shared INA219/INA226 16-bit register helpers.
- Do not add the INA228 register backend as part of this alignment unless that
  separately scoped implementation is explicitly started.
- Add regression checks that prevent alignment work from accidentally removing
  or narrowing the INA228 extension surface.

Future-facing surface is justified here by a concrete selected target, not by
speculative flexibility.

### Formatted logging remains the project mechanism

The structured-field logging requirement is unsuitable for this embedded
project and conflicts with the current fixed-record `logf` backend. This plan
does not redesign logging or mechanically rewrite existing log statements.

The contract source should instead say that logs:

- use the project logging facility;
- identify stable, searchable events in concise text;
- may format bounded scalar context into that text;
- avoid allocation, secrets, high-frequency progress output, and formatting in
  ISR or real-time output contexts; and
- use structured fields only where the active logging backend natively supports
  them.

### Locking across I/O needs a resource-lock exception

The default remains: do not hold an unrelated state or coordination lock across
a blocking call, I/O operation, callback, or unbounded loop.

The contract should explicitly permit a lock to span a bounded I/O transaction
when the lock owns and serializes that exact I/O resource. The exception applies
only when:

- serialization is required for correctness;
- the operation and timeout are bounded;
- no unrelated state lock is nested inside it;
- no user callback or unbounded work runs while it is held; and
- the reason is stated at the resource owner.

The I2C master lock qualifies because it serializes a shared physical bus
transaction. BatteryMonitor's state mutex around LittleFS work does not qualify
because the mutex protects estimator/calibration state rather than the
filesystem resource.

### `std::FILE` is deferred but explicitly tracked

The current LittleFS platform wrapper uses `std::FILE`, `fopen`, `fread`,
`fwrite`, `fflush`, and `fclose`. Runtime allocation and latency behavior have
not been established, so the filesystem layer cannot currently claim proven
allocation-free steady-state operation.

Replacing that backend is outside this implementation. Until a dedicated
filesystem correction is completed:

- do not broaden its use;
- do not claim that runtime journal I/O is allocation-free;
- measure journal operation latency and heap behavior during verification;
- keep all journal work out of callbacks and lock-protected sections; and
- record a follow-up task to evaluate preallocated handles/buffers and a more
  appropriate ESP-IDF-level file API.

## Protected invariants

The alignment must not regress these existing properties:

- INA226 manufacturer/die verification and calibrated measurements at `0x40`.
- INA219 and INA226 remain explicitly selected; there is no unreliable automatic
  model probe.
- INA228 remains free to use different register widths and accumulator behavior.
- I2C and user callbacks never run from the GPIO ISR.
- Callbacks use fixed inline storage and allocate nothing.
- A failed INA transaction never overwrites the last valid sample.
- Sampling remains rollover-safe and cooperative through `work(nowMs)`.
- Positive battery current means discharge; meaningful negative current means
  charging.
- Frequent measurements are integrated in RAM; persistent writes happen only
  at explicit calibration events.
- A reset during a write leaves preceding complete CRC-protected journal records
  readable, and a partial trailing record remains ignorable.
- Incomplete, invalid, corrupt, or interrupted calibration sessions are never
  activated as learned profiles.
- Pack-level monitoring remains operational guidance and never replaces the
  BMS.

## Scope

Expected implementation scope:

- contract source for logging and lock/I/O wording;
- `include/Wire/I2C/Interfaces/Ina2xxConfig.hpp`;
- `include/Wire/I2C/detail/Ina2xx.hpp` and focused INA2xx helpers;
- `include/BatteryMonitor/Interfaces/`;
- `include/BatteryMonitor/detail/`;
- a separate optional BatteryMonitor command adapter;
- `src/scratch/config.hpp` and `src/scratch/main.cpp`;
- focused host tests for pure and injected-storage behavior;
- `docs/wire-i2c.md`, `docs/battery-monitor.md`, and the two maintained
  implementation plans where behavior or verification records change.

Out of scope:

- implementing the INA228 register backend;
- replacing the shared filesystem `std::FILE` backend;
- a project-wide unit-type migration outside INA2xx/BatteryMonitor boundaries;
- redesigning the logging backend;
- opportunistic changes to unrelated lifecycle, mutex, command, or I2C users;
- preserving old internal APIs or an unused battery journal format when every
  producer and consumer can be updated together.

## Target design

### 1. BatteryMonitor has one runtime-state owner

The task that calls `observe()` and `work()` owns all mutable estimator,
calibration, finalization, and journal-scheduling state.

- `observe()` performs bounded arithmetic and state transitions without taking
  a mutex or accessing the filesystem.
- `work()` processes pending control requests, freshness checks, one bounded
  persistence/finalization step, metrics, and status publication.
- Public documentation states that `observe()` and `work()` must be called by
  the same task and are not reentrant.
- Cross-task commands do not mutate runtime state directly. They publish a
  fixed-size start/abort request through a single-slot atomic mailbox or an
  equivalently bounded project primitive.
- A full request slot is rejected as `Busy`; requests are never silently
  overwritten.
- The owner consumes at most one control request per `work()` call.

Status observation is separate from runtime ownership:

- The owner publishes a bounded status snapshot outside filesystem I/O.
- Readers copy only that snapshot under a short lock, or use a proven bounded
  sequence-lock snapshot if that is simpler with the platform primitives.
- A fallible snapshot operation returns an explicit result on lock timeout; it
  never proceeds without ownership.
- The synchronous INA sample callback never waits for a status reader.
- Journal/profile command output uses a compact public catalog summary rather
  than copying or exposing `detail::JournalScanResult`.

This removes the current unchecked-mutex behavior and prevents a slow LittleFS
operation from blocking sample consumption while holding BatteryMonitor state.

### 2. Commands become an optional adapter

Move command registration out of the core BatteryMonitor type.

- BatteryMonitor no longer inherits `HasCommands` or registers global commands
  during core `begin()`.
- Add an optional command adapter under a public modular subtree such as
  `BatteryMonitor/Commands/Adapter.hpp`.
- The adapter is explicitly instantiated and started by the environment setup.
- Status commands read the published snapshot.
- Start/abort commands submit bounded requests and report `Busy`, inactive,
  unavailable-storage, or accepted outcomes precisely.
- The command adapter does not receive access to estimator or journal internals.

This makes global command mutation visible at the setup seam and keeps the core
facade focused on battery behavior.

### 3. Storage health and retry behavior are explicit

Introduce a semantic storage state visible in BatteryMonitor status, for
example:

- `Unavailable`: filesystem not configured;
- `Healthy`: scan completed and writes may be attempted;
- `Corrupt`: the journal could not be scanned safely;
- `WriteFailed`: a scheduled append/flush/close failed;
- `Full`: the configured journal bound cannot accept a complete new session.

Required behavior:

- A scan failure leaves nominal runtime estimation available but disables new
  calibration.
- Calibration start requires `Healthy` storage and enough bounded journal space
  for the configured maximum session.
- Each header, interval, point, or footer event causes at most one immediate
  write attempt.
- A failed write invalidates the session, records the exact storage state, and
  clears or consumes the pending write so `work()` cannot retry it every loop.
- Recovery is explicit: reboot/rescan, an operator command, or a deliberately
  configured bounded retry policy. There is no implicit millisecond retry loop.
- Failure while writing a reboot footer is attempted at most once per boot
  unless an explicit retry is requested.
- Metrics distinguish scan, write, full, and retry/recovery outcomes where the
  distinction is operationally useful.

### 4. Journal growth and loops have declared bounds

Add an explicit upper bound rather than relying only on the finite LittleFS
partition.

- Add a maximum calibration duration or maximum interval-record count that is
  consistent with the expected 18-22 hour run and includes deliberate margin.
- Derive the maximum records for one session from that bound, the configured
  persistence interval, the fixed 101 profile points, header, and footer.
- Add a configured or compile-time maximum journal byte/record count.
- Reject a new calibration before writing its header when the complete bounded
  session cannot fit.
- Preserve all existing readable profiles on rejection; automatic destructive
  cleanup is not allowed.
- Keep explicit operator removal as the initial full-journal recovery policy.
- Stop scans at the declared maximum and report an oversized journal rather than
  traversing arbitrary content.
- Document every fixed buffer and the reject/drop behavior at its declaration.

The chosen defaults and worst-case byte calculation must be recorded in
`docs/battery-monitor.md` before implementation is considered complete.

### 5. Finalization is incremental

Replace the one-call complete journal scan in `buildCurve()` with a bounded
builder state machine.

- `beginCurveBuild()` opens/initializes the scan and fixed candidate storage.
- `workCurveBuild()` processes a small compile-time maximum number of journal
  records per call.
- Each interval may evaluate the 101 SOC targets; both loop bounds remain
  explicit and compile-time visible.
- `work()` performs at most one bounded builder step or one journal append per
  call.
- Profile point appends remain one point per call.
- The complete profile is activated only after its footer has been flushed and
  closed successfully.
- Errors close the builder, invalidate the session once, and do not create an
  automatic retry loop.

Reduce redundant profile storage while making this change:

- Build directly into the semantic `BatteryProfilePoint` representation where
  possible.
- Do not retain `remainingMilliampHours` or `remainingMilliwattHours` in each
  persisted point unless a consumer is added; both are derivable from SOC and
  totals and are currently ignored when profiles are loaded.
- Update the journal record producer, reader, CRC checks, docs, and host tests
  together. No migration reader is required unless valuable calibration data
  exists when implementation begins.

### 6. Power averaging and estimate validity are fixed

Replace the truncating integer EMA with fixed-point arithmetic that retains its
fractional adjustment.

- Keep a signed remainder or a compact Q-format accumulator.
- Preserve rollover-safe elapsed-time handling.
- Clamp or reject elapsed values outside the declared sampling bound before
  multiplication.
- Make a constant-load average stable.
- Make step-up and step-down responses monotonic.
- Ensure a zero/charging input can decay below the TTE threshold rather than
  freezing with a multi-watt residual.
- Reset all averaging remainder state when a discharge session is reset.

Estimate availability becomes explicit:

- Meaningful charging current makes TTE unavailable immediately.
- `work(nowMs)` marks the runtime measurement stale after the configured
  freshness tolerance even when calibration is inactive.
- Stale measurements cannot retain an available TTE.
- The next fresh measurement after a gap reinitializes integration according to
  the existing voltage/profile rules; it does not integrate across the gap.
- Status distinguishes never received, fresh, and stale measurements.
- Use `std::optional` for unavailable semantic values such as latest measurement
  and TTE rather than value-plus-validity booleans.
- Add a dedicated TTE-valid metric because confidence alone cannot distinguish
  low-load unavailability from a genuine zero-minute estimate.

### 7. Domain states are not operational failures

Change both components so a valid out-of-range measurement is not reported as a
failed operation.

INA2xx:

- I2C failure, invalid device response, and conversion/math overflow remain
  `ReturnCode` failures.
- Practical and absolute window states remain in `Ina2xxLimitState`, callbacks,
  latest sample state, metrics, and edge-triggered logs.
- A successfully converted absolute-limit sample no longer makes every
  `work()`/`sampleNow()` call return `Underflow` or `Overflow`.
- If `sampleNow()` returns the sample directly, use
  `std::expected<Ina2xxSample, ReturnCode>` so successful data and acquisition
  failure are unambiguous.
- Preserve callback ordering and document it at the public declaration.

BatteryMonitor:

- `observe()` returns an explicit observation value, optionally wrapped in
  `std::expected` for lifecycle/processing failures.
- `Absent`, practical, and absolute battery source states are successful
  observations carried in that value and status.
- Absolute states continue to invalidate an active calibration and produce an
  edge-triggered error event/log, but do not become repeated unhandled operation
  errors.
- The scratch integration calls `REPORT_IF_ERR` only for genuine unexpected
  failures.

### 8. Public types and configuration are tightened

Apply strong types at INA2xx/BatteryMonitor public boundaries without forcing an
unrelated repository-wide migration.

Candidate zero-overhead types include:

- milliseconds and captured timestamps;
- millivolts and microvolts;
- microamps;
- milliwatts;
- micro-ohms/milliohms;
- milliamp-hours and milliwatt-hours;
- minutes; and
- parts per thousand.

Implementation constraints:

- The wrapper is trivially copyable, standard-layout, allocation-free, and the
  same size as its representation.
- The generic template body remains small and constrained.
- Arithmetic helpers accept typed inputs and use checked wide intermediates.
- Raw register encoding/decoding is the explicit boundary where values enter or
  leave strong engineering-unit types.
- Metrics convert to their required 32-bit representation at the metrics seam.
- Do not introduce implicit conversions that recreate adjacent-integer hazards.

Also:

- Make config validation `constexpr` where all dependencies permit it.
- Make scratch INA/I2C configuration `inline constexpr` and retain focused
  `static_assert` validation.
- Replace or explicitly own stored path/name strings; no active component may
  retain an undocumented pointer to caller-owned temporary storage.
- Move any genuinely public journal/catalog status type into `Interfaces/`.
- Constrain deduced callback and classification templates with concepts.
- Add public Doxygen documentation for units, lifetime, ownership, task context,
  blocking, reentrancy, and callback ordering.
- Name dependency injection, adapter, facade, and calibration state-machine
  seams once where the code establishes them.
- Add missing INA I2C facade export annotations while touching that boundary.

### 9. INA228 surface is retained deliberately

Review rather than remove the current capability model.

- Separate `supportedCapabilities` from per-sample validity only if INA228 can
  produce temporarily unavailable accumulator or temperature values.
- If INA219/INA226 validity is constant, keep their implementation simple while
  preserving the semantic distinction needed by INA228.
- Document whether zero energy/charge/temperature is a real value or invalid on
  each model.
- Remove only fields proven redundant for all three intended models.
- Keep raw INA226-only status such as `maskEnable` out of a supposedly
  model-independent event unless a documented diagnostic consumer needs it.
- Ensure the future INA228 backend can supply 24-/40-bit native readings and
  explicitly scale, saturate, or expose them without silently truncating into
  the existing 32-bit metrics backend.

This review should end with a short capability table covering implemented
INA219/INA226 behavior and the protected INA228 target behavior.

## Contract corrections

### Logging wording

Replace the unconditional structured-field rule in both the general and C++
logging sections with the formatted-logging decision above. Keep the existing
rules for secrets, event-oriented logging, bounded cost, ISR restrictions, and
real-time output loops.

Acceptance check:

- Ordinary `_log_i/_log_w/_log_e` calls with bounded scalar formatting are
  contract-compliant.
- The contract does not require infrastructure the selected logger lacks.
- A future structured backend may use fields without forcing them on all
  embedded targets.

### Lock/I/O wording

Replace the unconditional prohibition with the default rule plus exact-resource
exception above.

Acceptance examples:

- I2C master mutex held across one transaction with a configured timeout:
  allowed and documented.
- Battery estimator mutex held while opening/flushing/closing LittleFS: not
  allowed.
- Lock held while invoking a user callback: not allowed.
- Lock held across an unbounded retry or scan loop: not allowed.

### Filesystem caveat

Add a project memory or tracked follow-up stating that the current ESP32
filesystem backend uses `std::FILE` and has not proven its steady-state heap and
latency behavior. Do not weaken the general no-allocation rule merely to bless
the current backend.

## Implementation phases

### Phase 0 - Baseline and contract correction

- [x] Record this plan as accepted and note whether valuable battery calibration
  data exists before changing the journal format.
- [x] Update the source contracts for formatted logging and bounded I/O resource
  locks.
- [x] Regenerate/reload the effective contract and confirm the intended wording
  appears in the hook output.
- [x] Record the `std::FILE` filesystem follow-up without implementing it.
- [x] Capture baseline `scratch` build size, `batteryMonitor`/`ina226` object
  sizes, and existing hardware metrics.
- [x] Confirm the only current runtime INA2xx/BatteryMonitor integration is
  `scratch` before breaking internal APIs.

### Phase 1 - Public result and status model

- [x] Define observation/sample outcomes that separate domain state from
  operation failure.
- [x] Add explicit fresh/stale/never-received and storage-health status.
- [x] Replace semantic value-plus-validity pairs with `std::optional`.
- [x] Add the compact public profile catalog summary.
- [x] Decide and document the focused strong-unit representation.
- [x] Update every affected call site and compile-time assertion together.

### Phase 2 - Battery ownership and command adapter

- [x] Make `observe()` and `work()` single-owner and non-reentrant.
- [x] Remove unchecked state guards from the sample path.
- [x] Implement the bounded control-request mailbox and explicit full behavior.
- [x] Publish a bounded status snapshot outside filesystem I/O.
- [x] Move command registration to the optional adapter and instantiate it
  explicitly in `scratch`.
- [x] Verify no command callback directly mutates estimator/calibration state.

### Phase 3 - Estimator correctness

- [x] Implement fixed-point/remainder-preserving average power.
- [x] Invalidate TTE immediately for charging and after sensor staleness.
- [x] Reset all relevant remainder and freshness state at session boundaries.
- [x] Add TTE validity metrics and update command rendering.
- [x] Add deterministic host tests for quantization, decay, charging, staleness,
  recovery, timestamp rollover, and long traces.

### Phase 4 - Persistence failure policy and bounds

- [x] Add bounded storage state and calibration-start space validation.
- [x] Add maximum session/journal record calculations and compile-time/runtime
  checks.
- [x] Consume failed pending writes so no per-loop retry is possible.
- [x] Disable calibration after scan corruption or unavailable/full storage while
  retaining nominal runtime estimation.
- [x] Implement explicit recovery behavior.
- [x] Add host tests for open/write/short-write/flush/close failures, storage
  capacity bounds, record corruption, and oversized-record bounds.
- [x] Add a retained-byte scanner test for partial writes and reboot
  interruption/recovery.

### Phase 5 - Incremental finalization and journal simplification

- [x] Replace the complete one-call curve scan with a bounded builder.
- [x] Process a fixed number of records per `work()` call.
- [x] Remove unused per-point remaining-capacity fields if no valuable persisted
  format must be retained.
- [x] Update writer, scanner, checksum validation, documentation, and tests in
  the same change.
- [x] Verify profile activation still occurs only after a complete valid footer.
- [ ] Measure worst observed finalization step time and task stack high-water
  mark.

### Phase 6 - INA2xx result semantics and interface tightening

- [x] Stop returning operational errors for valid absolute-limit samples.
- [x] Preserve edge-triggered practical/absolute events, logs, and metrics.
- [x] Keep INA228 temperature/energy/charge capabilities and document validity
  semantics per model.
- [x] Review `maskEnable`, `sampleValid`, and capability/validity fields against
  actual INA219/226/228 consumers; remove only cross-model redundancies.
- [x] Constrain callback and conversion templates.
- [x] Make configuration validation and scratch configuration `constexpr`.
- [x] Add public API/task-context documentation and facade annotations.

### Phase 7 - Integration verification

- [x] Build the host tests with no warnings or diagnostics.
- [x] Build `scratch` with `bin/build -e scratch` and inspect SARIF output.
- [x] Record RAM/flash deltas and `batteryMonitor`/`ina226` object sizes.
- [ ] Verify INA226 startup identity and live voltage/current/power metrics on the
  attached scratch hardware.
- [ ] Verify absolute/practical transitions no longer latch `UnhandledError`
  merely because a valid measurement is out of range.
- [ ] Exercise calibration start/abort and injected storage failures before the
  full discharge.
- [ ] Measure journal append/flush latency and heap before/after repeated runtime
  writes; label the result provisional until the `std::FILE` follow-up.
- [ ] Perform the battery-dependent cutoff/profile/TTE checks already listed in
  the BatteryMonitor implementation plan when the physical pack is ready.

### Phase 8 - Documentation and handoff

- [x] Update `docs/wire-i2c.md` for result semantics, model validity, ownership,
  and protected INA228 behavior.
- [x] Update `docs/battery-monitor.md` for ownership, freshness, storage health,
  bounds, retries, finalization cadence, and optional commands.
- [x] Update both maintained implementation plans and their verification
  records.
- [x] State which APIs and journal fields changed and confirm all in-repository
  producers/consumers were updated.
- [x] State exactly what was host-tested, built, measured on hardware, and left
  pending.

## Software verification record

- The journal version changed before any valuable calibration existed, so no
  field migration or retained user data was required.
- `bin/test-battery-monitor` passes the estimator, calibration state, journal
  format/CRC, complete-profile activation, retained-byte reboot/partial-write,
  bounded-capacity, and one-shot injected persistence-stage tests under C++23
  with `-Wall -Wextra -Werror`.
- `bin/build -e scratch` completes with no SARIF diagnostics. The final image
  uses 75,948 bytes RAM (23.2%) and 744,928 bytes flash (35.5%): 24 bytes less
  RAM and 4,824 bytes more flash than the pre-alignment build.
- Final object sizes are 3,640 bytes for `batteryMonitor` (80 bytes smaller than
  before alignment) and 696 bytes for `ina226` (unchanged).
- The aligned image has not been uploaded in this pass. INA226 live sampling,
  absolute/practical transitions, calibration commands, retained-byte reboot
  recovery on the target filesystem, finalization timing/stack, and LittleFS
  latency/heap remain explicit next-test or filesystem-follow-up work. Earlier
  battery-free hardware results remain recorded in the BatteryMonitor
  implementation plan.

## Host verification design

Add a small host-testable storage boundary rather than mocking the complete I2C
master or ESP filesystem.

The injected journal storage should support fixed-buffer implementations of:

- open/scan;
- append one complete record;
- short write;
- flush/close failure;
- capacity exhaustion;
- byte corruption;
- partial trailing record; and
- reboot by reconstructing BatteryMonitor over retained bytes.

Tests must cover:

- no filesystem call from `observe()`;
- no repeated write after one failed persistence event;
- control-request full behavior;
- unavailable/corrupt/full storage rejecting calibration;
- interrupted sessions never becoming active;
- incremental finalization respecting its per-call record budget;
- current and energy integration across timestamp rollover;
- EMA changes smaller than one output unit accumulating correctly;
- charging and stale data invalidating TTE;
- valid zero-minute TTE remaining distinguishable from unavailable TTE;
- absolute electrical states remaining successful observations;
- INA219/INA226 behavior remaining implemented; and
- the INA228 semantic capability surface remaining representable.

No new third-party test framework is required for the first pass. If a
persistent `test` environment is added, it must use the repository's permitted
future `test` role and remain focused on platform-independent headers.

## Acceptance criteria

The alignment is complete when:

- `observe()` cannot block on BatteryMonitor state ownership or touch LittleFS;
- no code continues after a failed mutex acquisition;
- no failed journal operation can be retried once per main-loop iteration;
- every runtime journal/finalization loop has an explicit bound;
- journal-full behavior rejects safely without deleting existing profiles;
- scan failure disables calibration rather than appending to unknown state;
- sub-milliwatt EMA adjustments accumulate instead of truncating permanently;
- charging and stale sensor data make TTE unavailable;
- availability is expressed with semantic types, not sentinel-plus-boolean pairs;
- valid absolute-limit samples are not reported as unhandled operation errors;
- command registration is explicit and optional;
- INA228 temperature/energy/charge support remains a protected public target;
- `scratch` builds cleanly and INA226 hardware behavior remains correct;
- host tests cover estimator, state-machine, persistence-failure, and recovery
  paths;
- RAM, flash, object size, stack, journal latency, and provisional heap behavior
  are recorded; and
- the effective contracts permit bounded formatted logging and necessary bounded
  I/O resource locks without permitting unrelated state locks across I/O.

## Deferred filesystem follow-up

Create a separate implementation plan after this alignment for the ESP32
filesystem platform layer. It should determine:

- whether newlib/ESP-IDF `std::FILE` operations allocate on each open or first
  use;
- whether buffering can allocate lazily after component initialization;
- the bounded latency of open, append, flush, and close on LittleFS;
- whether a preallocated descriptor/handle API is available and preferable;
- how to preserve current fixed-capacity path and chunk-reader behavior; and
- how commands and existing FileSystem consumers migrate without a compatibility
  layer during Dev phase.

Until that follow-up is complete, runtime filesystem use is an acknowledged,
measured exception candidate, not silently assumed compliant.
