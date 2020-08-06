History
=======

## 0.2.0

- added callback for allocating the transmit buffer with message type
  field for determining priority
- added message type field to write/send callback for determining
  priority
- changed URL schema to be an enum rather than string
- added mbed integration
- added wic_on_handshake_failure_fn callback to indicate a failed
  hanshake
- added wic_on_close_transport_fn to simplify the task of ensuring
  that the transport gets closed
- removed wic_send_pong* interfaces
- added on_ping and on_pong handlers


## 0.1.0

First release.
