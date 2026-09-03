# PubSub command transport implementation plan

Status: **planned**
Last updated: **2026-08-28**

## Goal

Add a bounded PubSub-backed transport for `CommandBackend` so a command can be
registered once on its owning node and invoked through either that node's
serial console or a targeted network request.

The first complete vertical slice is a reset command:

- `/reset` on a node's local console schedules a reset on that node;
- a PubSub command request containing `/reset` and targeting that node reaches
  the same registered handler;
- a request may target a mask of trusted nodes and receives one correlated
  completion result from each executing node; and
- reset does not require its own application PubSub topic or a second handler.

The work also corrects the current ownership inversion where starting a PubSub
node registers LED application commands. It does not replace typed application
events such as animation playback, FFT data, or LED PWM requests.

## Confirmed decisions

### Every connected node is trusted

This is a personal closed system. The command protocol does not add
authentication, authorization, signing, source allowlists, permissions, or
production hardening. CRC and ordinary PubSub validation continue to protect
against accidental corruption only.

Malformed input, queue pressure, duplicate delivery, and incomplete chunk sets
still need deterministic handling because those are correctness and recovery
concerns, not security policy.

### Command transport and application events remain distinct

A network command is a targeted request to invoke a command already registered
in the destination node's `CommandBackend` catalog. It uses the same parser,
descriptor, context, and handler as the local console.

An application event is a typed fact or intent consumed by components. Typed
animation messages remain appropriate because:

- master orchestration creates them without going through human command text;
- one publication fans out to every interested GPU;
- the payload is compact and versioned through the generated wire schema;
- playback/update request IDs carry domain-specific replacement and
  idempotency semantics; and
- GPU components consume typed data rather than depending on console syntax.

The intended relationship is therefore:

```text
local serial line --------------------+
                                      |
targeted PubSub command request ------+--> CommandBackend dispatcher
                                                   |
                                                   v
                                      one registered command handler
                                                   |
                         +-------------------------+--------------------+
                         |                                              |
                         v                                              v
              local SystemControl action                   typed domain action
                    (for /reset)                          (for /anim, /disp, ...)
                                                                        |
                                                                        v
                                                          application PubSub event
                                                                        |
                                                                        v
                                                               component consumers
```

Transport symmetry ends at the command handler. A handler may execute a local
service or publish a typed application event according to the command's domain.

### Application setup owns command registration

`PubSubBackend` must not include LED display command headers or register global
commands during `Node::begin()`.

Commands are registered explicitly by the node/application that owns their
behavior:

- common system commands are composed by `CoreSetup` where platform support is
  common;
- master registers the LED animation/display/layer publishing commands;
- power registers its BatteryMonitor command adapter;
- the owning GPU registers its local output-gate diagnostic command; and
- a node may explicitly opt into additional local diagnostics without changing
  PubSub core behavior.

This makes the command catalog predictable from each environment's setup and
prevents a transport lifecycle from silently changing application behavior.

### The command line is the network request surface

The initial protocol transports the same slash-prefixed command line accepted
by `ConsoleTransport`, rather than inventing numeric command IDs or a second
binary argument schema.

This deliberately keeps command lookup, subcommand selection, argument parsing,
defaults, validation, and handler invocation in one place. Typed application
wire formats remain below handlers where they are already useful.

## Current problems to remove

### PubSub currently registers LED commands

`PubSubBackend/detail/Node.hpp` includes
`Support/NetworkedCommands.hpp`, and `Node::_onBegin()` calls
`register_network_commands()`. That helper currently registers the LED display
commands on every PubSub node.

Consequences:

- PubSub core depends on a specific application subsystem;
- every PubSub-enabled environment receives command publishers whether or not
  it owns that interface;
- the command catalog changes as a side effect of network startup; and
- adding another so-called network command encourages another application
  dependency in PubSub core.

The new transport replaces this setup rather than extending it.

### `ITransport` cannot represent a network invocation

The current command transport interface returns only a span of tokens and a
display name. It cannot carry:

- a logical request/correlation ID;
- transport-owned completion context;
- a completion result;
- the source of an invocation; or
- a future bounded response sink.

`Controller` also logs dispatch failures locally and has no way to report the
final `ReturnCode` to the transport that supplied the command.

### Tokenization is private to the console

`ConsoleTransport` owns the slash check, whitespace tokenization, token bound,
and backing token array. A PubSub transport must not copy that parser and slowly
diverge from local console behavior.

### A PubSub payload cannot contain every valid console line

`CommandConfig::maxLineLen` is 255 bytes while
`PubSubConfig::maxPayloadSize` is 64 bytes. Several existing animation command
examples are longer than a useful single-frame command payload after target and
correlation metadata.

The network transport must either support the declared console bound or expose
a surprising smaller language. This plan supports the existing 255-byte bound
through fixed-size chunks and bounded reassembly.

## Scope

- Extract reusable command-line tokenization from `ConsoleTransport`.
- Extend the command transport/controller contract with invocation metadata and
  completion notification.
- Add `CommandRequestChunk` and `CommandResult` wire payloads and PubSub topics.
- Add a fixed-capacity PubSub command server transport on each networked MCU.
- Add a reusable command client/publisher for MCU and host-side command
  sources.
- Support single-node and node-mask targeting with one result per executor.
- Support command lines through `CommandConfig::maxLineLen` using bounded
  chunking and reassembly.
- Detect duplicate logical requests so retries cannot execute a handler twice.
- Remove LED command registration from `PubSubBackend::Node`.
- Register LED publisher commands explicitly on master.
- Add one common deferred `/reset` command as the end-to-end acceptance path.
- Add focused host tests and rebuild every active PubSub environment affected
  by the shared command and wire surfaces.
- Update `docs/commands.md`, `docs/pubsub.md`, and `docs/overview.md` where the
  implemented behavior changes their maintained reference material.

## Non-goals

- Do not add any security, authentication, authorization, or permission model.
- Do not replace typed animation, audio, input, power, or LED PWM PubSub
  payloads with command strings.
- Do not route high-rate or time-sensitive control through `CommandBackend`.
- Do not make command execution synchronous with end-to-end network delivery.
- Do not add a general RPC framework, service discovery system, dynamic command
  catalog synchronization, or remote object model.
- Do not migrate every existing command handler away from logging in this work.
- Do not redesign PubSub routing, transport reliability, or its general wire
  framing.
- Do not introduce general PubSub payload fragmentation. Reassembly belongs to
  the command adapter and applies only to `CommandRequestChunk`.
- Do not turn the master console into a general interactive remote shell in the
  first implementation. The reusable client API may support a small targeted
  command helper later without changing the server protocol.
- Do not refactor unrelated command adapters merely because they use
  `CommandBackend`.

## Target components

The names below are directional; exact file names may follow existing component
structure during implementation.

### Reusable command line tokenizer

Add a small `CommandBackend` tokenizer that accepts caller-owned line storage
and caller-owned token storage:

```cpp
struct TokenizedLine {
    std::array<CommandDesc::Token, CommandConfig::maxTokens> tokens;
    size_t count;
};

std::expected<CommandDesc::Tokens, ReturnCode>
tokenizeCommandLine(std::span<const char> line,
                    std::span<CommandDesc::Token> tokenStorage);
```

Required behavior:

- require one leading `/`;
- keep the existing whitespace-only token grammar;
- reject an empty command;
- reject more than `CommandConfig::maxTokens`;
- return views into the caller-owned complete line;
- allocate nothing; and
- return the same `CommandError` values currently produced by the console.

`ConsoleTransport` delegates final-line tokenization to this helper. Completion,
history, editing, echo, and UART event handling remain console-specific.

### Transport-neutral invocation

Replace the bare token span returned by `ITransport::poll()` with a small
invocation view:

```cpp
struct Invocation {
    CommandDesc::Tokens tokens;
    uint32_t correlationId = 0;
    uintptr_t transportCookie = 0;
};
```

The generic structure does not mention PubSub node IDs or envelopes. PubSub
metadata stays owned by the PubSub adapter and is reached through the opaque
cookie.

Extend `ITransport` with a completion hook:

```cpp
virtual std::expected<Invocation, ReturnCode> poll() = 0;
virtual ReturnCode complete(const Invocation &, ReturnCode result) = 0;
```

The console returns correlation ID/cookie zero and implements `complete()` as a
no-op. The PubSub transport uses the cookie to retain source/request metadata
until it publishes a result.

The lifetime contract is explicit: token views and transport cookies remain
valid until `Controller` has synchronously dispatched that invocation and
called `complete()` exactly once. `Controller` must call `complete()` for both
successful and failed dispatches, including missing commands and argument
errors.

If publishing a completion result fails, that transport failure is logged and
counted without executing the command a second time or stopping the command
task.

### Command request wire payload

Use one fixed-size payload whose encoded size does not exceed the current
64-byte PubSub maximum:

```cpp
struct WIRE_MSG CommandRequestChunk {
    uint32_t requestId;
    Totem::Data::PubSub::NodeId targetNodes;
    uint16_t totalLength;
    uint16_t offset;
    uint8_t chunkLength;
    std::array<std::byte, 53> bytes;
};
```

The proposed metadata is 11 bytes, leaving 53 bytes for command text. The
exact array size must be derived or asserted against the generated codec so a
future field change cannot silently exceed `maxPayloadSize`.

Protocol rules:

- `requestId` identifies the logical command and is independent of each
  PubSub envelope's message ID;
- `(envelope.source, requestId)` uniquely identifies one logical request;
- `targetNodes` is a nonzero mask of `NodeId` values;
- `totalLength` is in `1..CommandConfig::maxLineLen`;
- offset and length must remain within `totalLength`;
- the complete reassembled line includes its leading `/`;
- all chunks for a logical request must agree on target and total length;
- chunks may arrive out of order;
- exact duplicate chunks are harmless;
- conflicting overlaps reject the logical request; and
- bytes after `chunkLength` in the fixed array are ignored and should be zeroed
  by publishers for deterministic traces.

At the current bounds, a maximum-length command requires five chunks.

### Command result wire payload

Publish one final result per node that executes or rejects a complete targeted
request:

```cpp
enum class CommandResultState : uint8_t {
    Completed,
    Rejected,
};

struct WIRE_MSG CommandResult {
    uint32_t requestId;
    Totem::Data::PubSub::NodeId requester;
    Totem::Data::PubSub::NodeId executor;
    CommandResultState state;
    ErrorDomain errorDomain;
    uint8_t errorCode;
};
```

`Completed` means the command handler returned; `errorDomain/errorCode` retain
its exact `ReturnCode`, including successful completion. `Rejected` means the
complete request could not be offered to the dispatcher, for example because
the command line was invalid or the ready-command queue was full.

Once a valid targeted chunk has allocated a request slot, the receiver knows
the requester and logical request ID from the envelope/chunk metadata. An
incomplete request that expires therefore publishes `Rejected` with
`CoreError::Timeout`; result-publication failure is counted and logged locally.
Malformed chunks that do not contain enough valid metadata to identify a
logical targeted request are dropped without a result.

The result deliberately does not capture arbitrary log output. Action commands
such as animation requests and reset receive precise success/failure results.
If a later remote status command needs text, add a bounded command-output sink
and `CommandOutputChunk` as a separate scoped extension; do not scrape or reroute
the global logger.

### PubSub topics and traffic policy

Add two topics using the next available topic bits:

- `CommandRequest`
- `CommandResult`

Every networked node that accepts commands subscribes to `CommandRequest` and
filters `targetNodes` locally. This creates low-rate topic fanout but avoids
spending one topic bit per node and avoids coupling PubSub routing to command
payload fields.

Command requests and results use `TrafficClass::Critical` and do not require a
synced clock. Administrative recovery commands must remain usable during clock
startup or resynchronization. Add a narrowly scoped publish-options overload to
`PubSubService::publish()` if necessary instead of duplicating the complete pool
and envelope-publishing implementation in the command adapter.

### Bounded request reassembler

The PubSub subscriber callback validates and copies each targeted chunk into a
fixed-capacity reassembly table. It never tokenizes or dispatches a command in
the PubSub task.

Each request slot owns:

- source node ID and request ID;
- target mask and total length;
- first/last activity timestamp;
- `CommandConfig::maxLineLen + 1` bytes of line storage;
- received-chunk/byte coverage;
- transport-owned token storage; and
- a state that distinguishes assembling, ready, dispatching, and completed.

Start with one small unified table, proposed as four request slots per node. A
ready queue contains only slot indices, so the line and token backing never
move between reassembly and dispatch. Declare both the slot count and ready
queue depth in a command-PubSub config rather than increasing general PubSub
limits. A full table rejects the new logical request when its valid metadata is
enough to publish a result; it never evicts a partially assembled older request
silently.

Reassembly timeout is bounded and rollover-safe. A proposed initial timeout is
1000 ms, long enough for five low-rate frames while preventing abandoned slots
from becoming permanent. Timeout cleanup runs from adapter work/task context,
not from an ISR.

When coverage is complete:

1. NUL-terminate the transport-owned line for diagnostics only.
2. Tokenize it through the shared tokenizer into transport-owned token storage.
3. Change the slot to `ready` and enqueue its index once.
4. Wake `CommandBackend::Controller`.
5. Keep the backing line and tokens stable until `complete()`.

The subscriber callback performs only bounded validation, copying, slot scans,
queue submission, result rejection when possible, metrics, and task
notification. The slot is released only after `complete()` has copied its final
result into the recent-completion cache. The opaque transport cookie should
encode a slot index plus generation so a stale completion can be rejected
without dereferencing reused state.

### Duplicate suppression

Retain a small fixed cache of recently completed `(source, requestId)` keys and
their final `CommandResult`.

- A replay received while the original request is assembling merges normally.
- A replay received while it is ready or dispatching does not enqueue another
  invocation.
- A replay received after completion republishes the cached result when
  practical and never executes the handler again.
- Entries expire after a declared bounded interval or by deterministic oldest
  replacement.

This gives at-most-once handler execution within the cache window without
claiming durable exactly-once behavior across MCU reset.

### PubSub command server transport

Add an integration component outside both subsystem cores, for example:

```text
include/CommandPubSub/
  Facade.hpp
  Interfaces/Config.hpp
  Interfaces/Wire.hpp
  detail/Reassembler.hpp
  detail/ServerTransport.hpp
  detail/Metrics.hpp
```

`ServerTransport` implements `CommandBackend::ITransport` and owns the
`CommandRequest` subscription, reassembly slots, ready queue, duplicate cache,
and completion publisher.

Lifecycle ordering is explicit:

1. Construct the adapter with the command controller/wake callback.
2. Add it to `CommandBackend::Controller` before the controller begins, just as
   the console transport is added.
3. Complete `CoreSetup`, including metrics prewarming and command-controller
   startup.
4. Start the node's PubSub network and bind `PubSubService`.
5. Begin the command PubSub server, prewarm its metrics, and subscribe to
   `CommandRequest` before command traffic is enabled.

Do not add transports to the command controller concurrently with its task
loop. If current setup composition makes the pre-begin step awkward, add a
small `CoreSetup::addCommandTransport()` forwarding method rather than making
controller transport registration dynamically synchronized.

`ServerTransport::end()` unsubscribes, prevents new callback work, drains or
rejects retained invocations deterministically, destroys its queue, and then
releases reassembly storage.

### Reusable command client

Add a small publisher that accepts a target mask and slash-prefixed line,
allocates one logical request ID, divides the line into canonical chunks, and
publishes them in offset order:

```cpp
std::expected<uint32_t, ReturnCode>
send(NodeId targetNodes, std::string_view commandLine);
```

The client validates the complete command before publishing the first chunk. A
partial publish failure returns the failure with the request ID available for
diagnostics; receivers eventually time out incomplete reassembly.

The client does not wait synchronously. An optional result subscriber correlates
`CommandResult` records for UI, host tooling, or later master-side workflows.
Waiting for all bits in a target mask is a caller policy with a timeout, not a
blocking embedded command API.

Extend the existing host PubSub tooling with the smallest useful command entry
point, for example:

```text
bin/pubsub-command --target power /hello
bin/pubsub-command --target gpu0 /reset
```

It should use the generated wire model or the same C++ wire definitions rather
than maintaining hand-written offsets. The tool prints one correlated result
per executor and times out clearly when an expected node does not answer.

Host tooling is an adapter over the protocol, not a second command catalog.

## Command ownership after the change

### Remove implicit network command registration

Delete the `Support/NetworkedCommands.hpp` dependency and
`register_network_commands()` call from `PubSubBackend::Node`. Remove the helper
entirely if no explicit application setup still needs that name.

`Node::begin()` returns to owning only PubSub lifecycle, queues, metrics, task
startup, transports, subscriptions, and routing.

### Make LED publisher commands explicit

Master explicitly starts the LED command adapter that registers `/anim`,
`/disp`, and `/layer`. GPU nodes continue subscribing to typed animation
topics through `LedDisplay`; they do not need those publishing commands merely
because they run PubSub.

If direct GPU-console publication remains useful for bench diagnostics, make it
an explicit GPU setup decision. Do not restore it through a shared PubSub hook.

Convert `register_display_commands()` into a small lifecycle-aware adapter only
if needed to make registration rollback and environment ownership clean. Keep
that change limited to the existing three LED command roots; do not migrate
unrelated command providers.

### Keep component command adapters local

The existing BatteryMonitor adapter and GPU output-gate command already model
the desired explicit composition. Leave them in their owning environments.
They automatically become remotely invocable when that node has the PubSub
command server transport; no command-specific PubSub event is added.

## Reset vertical slice

Add one common system command with no command-specific network code:

```text
/reset
```

The handler calls a narrow platform-independent restart scheduler rather than
calling ESP-IDF directly or rebooting inside the command task.

Required behavior:

- only one reset may be pending;
- repeated requests while pending return a stable successful/already-pending
  outcome rather than moving the deadline indefinitely;
- the handler returns before the reset occurs;
- the PubSub transport can publish the final command result;
- `CoreSetup::work(nowMs)` or another existing cooperative system owner performs
  the actual platform restart after a short fixed drain interval;
- the drain interval is declared and rollover-safe;
- local console and PubSub invocation reach exactly the same handler and
  scheduler; and
- host tests inject a restart hook and never call a real platform reboot.

A proposed initial drain interval is 250 ms. This is an operational delay, not
a claim that delivery to the requester is guaranteed. The command logs one
concise scheduled-reset event and does not block, sleep, allocate, or reset from
inside a callback.

Do not add `ResetRequest`, `ResetEvent`, or a reset-specific PubSub topic.

## Adjacent friction cleanup

The following work is close enough to the target path to record, but remains
bounded.

### Perform now

- Extract the console tokenizer because the new transport requires identical
  syntax.
- Make application command registration explicit because the current PubSub
  ownership is directly in the way.
- Give the command controller a transport completion seam because otherwise a
  network transport cannot report dispatch failure precisely.
- Add publish options to the existing facade only as needed for critical,
  pre-clock-sync command traffic.
- Use lifecycle-aware rollback for the touched LED command adapter if explicit
  registration exposes partial-registration failure.

### Record as a narrow follow-up

Animation command construction currently has a useful typed `Config` and
generated `FieldList`, but ordinary CLI descriptors repeat field names, parsing,
defaults, and clamping. A later animation-only follow-up may:

- move normalization and validation into the shared config/command-construction
  path so console and master orchestration cannot disagree;
- introduce a generic play action taking `Config` plus common play options;
- derive ordinary CLI field names and scalar parsing from generated wire field
  metadata; and
- retain explicit command descriptors only for nonstandard syntax or update
  behavior.

That follow-up must preserve non-obvious argument documentation and should be
proved on one representative animation before any mechanical migration. It is
not required to land the PubSub command transport or `/reset` and must not turn
into a general reflection or command-framework rewrite.

### Explicitly leave alone

- command adapters unrelated to the new transport or ownership inversion;
- animation rendering, layering, playback/update semantics, and PubSub topics;
- the generated wire system outside the two new payloads and normal regeneration;
- logging architecture; and
- the command registry's general storage model.

## Metrics and diagnostics

Prewarm command-PubSub metrics before subscribing or accepting traffic. Useful
bounded counters are:

- request chunks received;
- chunks ignored because the local node is not targeted;
- malformed or conflicting chunks;
- reassembly slots exhausted;
- reassembly timeouts;
- completed lines queued;
- ready-queue drops;
- invocations dispatched;
- command successes and failures;
- duplicate requests suppressed;
- cached results replayed; and
- result publication failures.

Do not log every normal chunk. Log completed requests at debug level with source,
request ID, target, and bounded line length; never assume command text is safe
to print indefinitely. Warnings identify malformed requests, bounded resource
pressure, timeouts, and completion publication failures with stable searchable
text.

## Implementation phases

### Phase 1: CommandBackend invocation foundation

1. Extract and host-test the reusable command-line tokenizer.
2. Change `ITransport::poll()` to return `Invocation`.
3. Add the completion callback contract.
4. Update `Controller` to call completion exactly once after every dispatch
   attempt.
5. Adapt `ConsoleTransport` with no user-visible behavior change.
6. Add focused fake-transport tests for success, unknown command, bad arguments,
   poll errors, and completion errors.

Phase exit condition: existing local console behavior and command discovery are
unchanged, while a fake transport receives the exact final `ReturnCode` for
each offered invocation.

### Phase 2: Wire protocol and pure reassembly logic

1. Add the two topics and wire payload types.
2. Regenerate C++ and Python wire metadata through the repository generator.
3. Add static encoded-size assertions.
4. Implement the fixed-capacity reassembler independently of live PubSub.
5. Test single-chunk, five-chunk, out-of-order, duplicate, conflicting,
   oversized, wrong-target, timeout, and slot-pressure cases.
6. Implement the recent-completion cache and prove at-most-once queueing within
   its window.

Phase exit condition: arbitrary valid console lines through the 255-byte bound
produce exactly one stable tokenizable line or a precise bounded rejection.

### Phase 3: PubSub server transport and client

1. Implement `ServerTransport` subscription, queue, wake, poll, and completion
   publication.
2. Add the critical/pre-sync publish option without duplicating generic PubSub
   pool ownership code.
3. Implement the nonblocking C++ command client/chunker.
4. Add a host sender using generated wire metadata.
5. Exercise request/result correlation through host tests or the existing local
   PubSub harness.

Phase exit condition: a host or simulated client can invoke `/hello` on one
target node, receive one successful correlated result, and prove that other
nodes did not dispatch it.

### Phase 4: Composition and ownership correction

1. Remove command registration from `PubSubBackend::Node`.
2. Delete or retire `Support/NetworkedCommands.hpp`.
3. Compose one server transport explicitly into every active PubSub environment.
4. Register LED publisher commands explicitly on master.
5. Preserve power's BatteryMonitor adapter and GPU-local gate command in their
   current owning setups.
6. Verify command counts and `/help` on each node reflect only explicitly owned
   commands.

Phase exit condition: PubSub begins without registering any application
command, and each environment's intended local command catalog remains usable.

### Phase 5: Deferred reset command

1. Add the restart scheduler and injected platform restart hook.
2. Register `/reset` once through the common system-command composition.
3. Test direct console/backend dispatch and PubSub dispatch against the same
   scheduler instance.
4. Publish the command result before the cooperative reset deadline.
5. Bench-test one targeted edge-node reset and recovery.
6. Bench-test a multi-node target mask only after the single-node result and
   recovery path is observable.

Phase exit condition: adding reset required one command handler and no
reset-specific network event or subscriber.

### Phase 6: Documentation and bounded adjacent cleanup

1. Update the maintained command, PubSub, overview, and command-discovery docs.
2. Remove stale wording that animation commands are registered by PubSub node
   startup or are available on every node implicitly.
3. Record measured queue/timeout/config values.
4. Decide separately whether the animation-only schema/normalization follow-up
   has enough value to start; do not fold it into transport completion by
   default.

## Validation plan

### Host tests

Add a dedicated host command test target or extend the nearest existing host
test only if it remains focused and discoverable.

Required cases:

- console tokenizer compatibility for empty, missing slash, whitespace,
  maximum tokens, excessive tokens, and maximum-length lines;
- controller completion on success and every dispatch failure path;
- completion callback failure does not dispatch twice or stop the controller;
- exact wire round trips for request chunks and results;
- encoded request size does not exceed 64 bytes;
- single- and multi-chunk reconstruction;
- out-of-order and exact duplicate chunks;
- conflicting overlap and inconsistent metadata rejection;
- wrong-target chunks do not consume reassembly capacity;
- incomplete request timeout and slot reuse;
- timeout rejection is correlated to the requester when valid request metadata
  was received;
- ready-queue backpressure is explicit;
- duplicate logical requests execute once;
- node-mask requests produce one result per simulated executor;
- reset handler schedules once and injected restart occurs only after the drain
  interval; and
- console and PubSub paths invoke the same reset handler/context.

Add or update repository commands such as:

```sh
bin/test-command-backend
bin/test-pubsub-wire
```

The exact script split should follow existing host test wrappers and avoid a
catch-all test executable.

### Firmware builds

Because this touches shared CommandBackend, PubSub topics/wire code, setup
composition, and all active network nodes, build:

```sh
bin/build -e master -e media -e power -e gpu0 -e gpu1 -e io
```

Inspect the per-environment SARIF output on failure. `ai` and `scratch` should
also be built if common `/reset` registration or `CoreSetup` changes compile
through those standalone environments.

### Bench validation

1. Confirm ordinary local `/help` and `/hello` still work on representative
   nodes.
2. Send network `/hello` to one edge node and observe one success result.
3. Repeat the same request ID and confirm the handler is not run twice.
4. Send a maximum-length harmless test command or tokenizer fixture and confirm
   five-chunk reconstruction.
5. Send a request to a two-node mask and receive two executor results.
6. Invoke local `/reset` on one edge and confirm the scheduled-reset log and
   normal reboot.
7. Invoke network `/reset` on one edge, observe its successful command result,
   reboot, link recovery, and subscription replay.
8. Confirm a non-targeted neighboring node remains up and does not log command
   dispatch.
9. Re-run a representative `/anim` command on master and verify both GPUs still
   consume the existing typed animation publication.

## Acceptance criteria

- `PubSubBackend::Node` has no dependency on LED display commands or any other
  application command catalog.
- A node's command catalog is determined by explicit application composition.
- `ConsoleTransport` and PubSub command transport use one tokenizer and one
  dispatcher.
- A network request supports every line length accepted by the declared command
  configuration.
- PubSub callbacks never execute command handlers directly.
- Fixed capacities, queue behavior, timeouts, and duplicate behavior are
  declared and tested.
- A logical request executes at most once within the duplicate-cache window.
- The requester receives the exact handler `ReturnCode` from each targeted
  executor that completes dispatch.
- `/reset` is implemented once and works through both local console and PubSub
  transport.
- No reset-specific PubSub application topic or subscriber exists.
- Existing typed animation publications remain the master-to-GPU domain path.
- The implementation adds no authentication or authorization code.
- All affected host tests pass and all active network environments build.
- Documentation describes the explicit command ownership and the distinction
  between remote command invocation and typed application events.

## Risks and guardrails

### Token lifetime

`CommandDesc::Tokens` contains views. Never release or reuse a transport slot
until dispatch and completion return. Tests must deliberately enqueue another
request after completion to catch stale-view mistakes.

### Task ownership

PubSub callback context only assembles and queues. Command handlers run in the
existing command task. Reset runs later from its cooperative owner. Do not add
blocking waits between these tasks.

### Fanout and loops

All command servers subscribe to one request topic, so local target filtering
must happen before slot allocation. A command handler must not republish its
original generic request. Typed `/anim` output is a different topic and remains
safe.

### Partial publication

The client may publish some chunks and fail on a later one. Receivers time the
request out; they never dispatch an incomplete line. Do not add unbounded retry
inside the publisher.

### Reset result timing

Returning from the handler means reset was scheduled, not that reboot already
occurred. Keep the result wording and enum semantics precise. The drain delay
improves observability but is not a delivery guarantee.

### Scope growth

Do not use the new invocation type as a reason to redesign handlers, registrars,
logging, metrics, every command adapter, or every application wire message. Only
the transport contract, explicit ownership seam, reset slice, and directly
required helpers belong in this implementation.

## Expected file areas

Primary implementation areas:

- `include/CommandBackend/Interfaces/ITransport.hpp`
- `include/CommandBackend/detail/Controller.hpp`
- `include/CommandBackend/Transports/ConsoleTransport.hpp`
- a new reusable tokenizer under `include/CommandBackend/detail/`
- a new `include/CommandPubSub/` component
- `include/Data/PubSub.hpp`
- new generated wire payload inputs/outputs under the normal wire workflow
- `include/Services/PubSub.hpp`
- `include/PubSubBackend/detail/Node.hpp`
- `include/Support/NetworkedCommands.hpp` removal
- `include/LedDisplay/Interfaces/Commands.hpp` or a narrowly renamed LED command
  adapter
- `include/Setups/Core.hpp`
- active environment setup files under `src/`
- focused files under `test/` and `bin/`
- existing host PubSub tooling for the command sender
- `docs/commands.md`
- `docs/pubsub.md`
- `docs/overview.md`

Generated wire outputs should be regenerated through project commands rather
than edited manually.
