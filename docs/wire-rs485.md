#RS485 Wire Layer

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

Frame semantics are intentionally split:

- `Data` expects a link `Ack` / `Nack`. This is the path intended for a future
  PubSub RS485 transport; the transport can release its egress buffer when the
  RS485 write callback receives the link ack.
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
- `Nop` is a zero-payload skip marker. A node sends it when it owns an
  initiating write slot but has no data/exchange/heartbeat to send. The peer
  responds with a correlated `Nop`, allowing both sides to advance the shared
  bus cycle without relying on timing-only slot skips.

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
the higher-level payload owner such as `Clock` or future `PubSub`. Consumers can
register a `FrameHandler` with an RS485 node instead of repeatedly submitting
read requests.

Clock synchronization in `include/Clock/detail/` is the first L7 protocol on top
of this exchange path. The Clock slave sends a `Clock` `Request`, the Clock
master acts as the time server and returns a `Clock` `Response`, and the slave
computes drift from its local send/receive times plus the master's receive/send
times.
