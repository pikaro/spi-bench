# RS485 Wire Layer

`include/Wire/Rs485/detail/` separates the point-to-point RS485 stack into
small layers:

- `Pdu.hpp`: frame type, payload type, sequence, response correlation, length,
  and header CRC.
- `Transceiver.hpp`: UART-backed frame I/O plus the half-duplex turn guard. It
  does not decide protocol reactions, but it rejects frame reads or writes that
  do not match the current master/slave bus slot.
- `Node.hpp`: task ownership, queues, registered payload handlers, and
  transaction execution.
- `Master.hpp` / `Slave.hpp`: role-specific handshake policy.

The ESP32 UART wrapper uses normal two-pin UART routing for RS485. Configure
separate TX/DI and RX/RO GPIOs; the RS485 transceiver does not enable one-wire
UART mode.

Frame semantics are intentionally split:

- `Data` expects a link `Ack` / `Nack`. This is the path used by the PubSub
  RS485 transport; the transport can release its egress buffer when the RS485
  write callback receives the link ack.
- `Request` expects a `Response` and does not add an intermediate link ack.
  This is the low-overhead path for time-sensitive protocols such as Clock
  synchronization.
- `Poll` also expects a `Response`, but its name records the bus-control intent:
  the master is granting or requesting that the slave speak.
- `Hello` is the two-way startup handshake. The master is the active party.
- `Heartbeat` is a zero-payload link control frame sent by the master after
  handshake. The slave responds with a `Heartbeat` correlated through
  `responseTo`; either side resets to handshake mode when the peer misses three
  heartbeat windows.
- `Grant` is a zero-payload, one-way turn marker sent by the master when the
  attention line says a slave has queued traffic. It advances the split turn
  from master write directly to slave write, avoiding a dummy response before
  the slave's real frame.
- `Nop` is a zero-payload turn marker. The master no longer sends periodic idle
  nops while the attention line is quiet. A slave can still send `Nop` when it
  owns an initiating write slot but has no data/exchange to send, allowing the
  master to advance back to its write slot after handshake, heartbeat, or
  master-originated traffic.

The transceiver enforces an explicit split turn cycle. A master starts in
`WriteRequest`, then reads the reaction, then reads the peer's request, then
writes the reaction. A slave starts in the opposite half: read request, write
reaction, write request, read reaction. Empty nonblocking reads do not advance
the cycle; only an actual frame read or write advances the turn. Reconnect and
handshake retry reset the turn to the role's starting state.

On ESP32, the UART driver is installed with an event queue. The platform UART
wrapper maps driver events to platform-agnostic `UartEventType` values and
invokes registered callbacks from a small event task. RS485 registers a callback
that wakes its task with `UartData`, `UartOverflow`, or `UartError`, so inbound
frames do not wait for the periodic task interval before being polled.
The RS485 runner keeps a slower periodic cadence for watchdog coverage and
handshake/heartbeat housekeeping; normal traffic should be driven by UART and
attention notifications rather than by a tight polling loop.

RS485 can also use an active-low attention GPIO as a side-band wake signal. The
slave drives this line with an open-drain output while it has queued traffic;
the master samples the pulled-up input and uses a GPIO interrupt to wake the
RS485 runner immediately. The attention line only requests a bus turn. All data
still flows through RS485 frames, and the normal turn guard remains authoritative.

Reconnect is intentionally lossy at the RS485 transaction layer. When the link
resets, the node discards pending UART input and nacks queued writes/exchanges
so old PubSub or Clock transactions cannot leak into the next handshake with a
stale sequence number. While a node is handshaking, non-`Hello` frames are
treated as stale input and discarded instead of sequence-checked against the new
connection.

The second header discriminator is `Totem::Wire::PayloadType`, which identifies
the higher-level payload owner such as `Clock` or `PubSub`. Consumers can
register a `FrameHandler` with an RS485 node instead of repeatedly submitting
read requests.

Clock synchronization in `include/Clock/detail/` is the first L7 protocol on top
of this exchange path. The Clock slave sends a `Clock` `Request`, the Clock
master acts as the time server and returns a `Clock` `Response`, and the slave
applies the drift returned by the master. The request payload carries the
slave's local request marker timestamp; RS485 patches that marker immediately
before writing the request frame, and the master computes
`drift = masterReceiveTime - slaveMarkerTime`. This is the same Clock payload
protocol used by SPI: the response carries the drift plus a validity flag, and
the slave applies only valid samples. Unlike the old four-timestamp exchange,
response send time and response receive time are not part of the Clock
calculation.
