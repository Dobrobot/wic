History
=======

## 0.2.0

Many enhancements to the original concept, the most significant being improved
handshaking and transport congestion handling.

- added `wic_on_buffer_fn` callback for allocating a transmit buffer
  and propagating backpressure when socket is not able to buffer
- added buffer type field to `wic_on_buffer_fn` and `wic_on_send_fn`
  to support buffer prioritisation (i.e. so a pong doesn't disappear)
- added `wic_on_handshake_failure_fn` callback to indicate a failed handshake
- added `wic_on_close_transport_fn` callback as a dedicated handler for
  closing the transport
- added `wic_on_ping_fn` callback
- added `wic_on_pong_fn` callback
- removed `wic_send_pong*` interfaces
- changed URL schema to be an enum rather than string
- added mbed port 

## 0.1.0

First release.
