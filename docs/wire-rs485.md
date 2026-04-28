#RS485 Wire Layer

`include/Wire/Rs485/detail/` separates the point-to-point RS485 stack into
small layers:

- `Pdu.hpp`: frame type, payload type, sequence, response correlation, length,
  and header CRC.
- `Transceiver.hpp`: UART-backed frame I/O only. It does not decide protocol
  reactions.
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

The second header discriminator is `Totem::Wire::PayloadType`, which identifies
the higher-level payload owner such as `Clock` or future `PubSub`. Consumers can
register a `FrameHandler` with an RS485 node instead of repeatedly submitting
read requests.

Clock synchronization in `include/Clock/detail/` is the first L7 protocol on top
of this exchange path. The Clock slave sends a `Clock` `Request`, the Clock
master acts as the time server and returns a `Clock` `Response`, and the slave
computes drift from its local send/receive times plus the master's receive/send
times.
