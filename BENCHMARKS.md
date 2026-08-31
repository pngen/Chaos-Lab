# Benchmarks

Chaos Lab measures actual completed laboratory work, not no-op functions. Values
below are from the Debug build on a development workstation (MSVC 19.44, 16
logical processors, RTX 5090 / sm_120, 32 GiB VRAM, 64 GiB host). They are real
measurements of that machine and are not intended as cross-machine guarantees.

Run the benchmark executable after building:

    chaos_bench

## Measured

| Operation | Time | Notes |
| --------- | ---- | ----- |
| Evidence ingestion | 788.94 ms | 100,000 records recorded |
| Evidence serialization | 81.11 ms | 2,538,890 bytes of deterministic text |
| Evidence reload (parse) | 612.84 ms | 50,000 records |
| Evidence state digest | 79.90 ms | 50,000 records |
| SHA-256 digest | 232.64 ms | 200,000 hashes |
| Fault schedule / plan | 185.72 ms | 2,000 faults planned and observed |
| Frame codec decode | 65.91 ms | 200,000 frames (128-byte payload) |
| Framed-proxy throughput (no fault) | 313.40 ms | 2,000 frames over loopback TCP; 4,000 forwarded (dup+echo) |
| Process launch + cleanup | 290.70 ms | 20 spawned and reaped processes |

## What is measured

- **Event-record ingestion**: the cost of recording a structured evidence item.
- **Assertion evaluation**: covered by evidence reload + digest recomputation.
- **Process launch/cleanup**: CreateProcess, wait, handle close, and cleanup.
- **Fault scheduling**: planning a deterministic schedule from a seed and stepping
  an observed event stream to fire injections.
- **Framed-proxy throughput**: real loopback TCP forwarding through the faultable
  interposer in the no-fault case. Deterministic injection (integer drop /
  corrupt) has a per-frame match cost; fault counts are exact and reproducible.
- **Evidence serialization/reload**: deterministic text and JSON.
- **Concurrent event processing**: the transport interposer runs two forwarder
  threads per connection; process/event handling is single-writer per connection.

The proxy throughput in the no-fault case reports 4,000 frames forwarded for 2,000
sent because the interposer forwards both directions (client->server and the
echoed server->client). All measurements are wall-clock on this machine.
