# Benchmarks

Chaos Lab measures actual completed laboratory work, not no-op functions. Values
below are from the **Release** build on a development workstation (MSVC 19.44,
16 logical processors, RTX 5090 / sm_120, 32 GiB VRAM, 64 GiB host). They are
real measurements of that machine and are not intended as cross-machine
guarantees.

Run the benchmark executable after building:

    chaos_bench

## Measured (Release)

| Operation | Time | Notes |
| --------- | ---- | ----- |
| Evidence ingestion | 67.26 ms | 100,000 records recorded |
| Evidence serialization | 8.89 ms | 2,538,890 bytes of deterministic text |
| Evidence reload (parse) | 43.40 ms | 50,000 records |
| Evidence state digest | 12.53 ms | 50,000 records |
| SHA-256 digest | 48.25 ms | 200,000 hashes |
| Fault schedule / plan | 19.52 ms | 2,000 faults planned and observed |
| Frame codec decode | 7.34 ms | 200,000 frames (128-byte payload) |
| Framed-proxy throughput (no fault) | 312.78 ms | 2,000 frames over loopback TCP; 4,000 forwarded (dup+echo) |
| Process launch + cleanup | 296.31 ms | 20 spawned and reaped processes |

## What is measured

- **Event-record ingestion**: the cost of recording a structured evidence item.
- **Assertion evaluation**: covered by evidence reload + digest recomputation and
  the assertion evaluator (see the assertion-engine coverage).
- **Process launch/cleanup**: CreateProcess, wait, handle close, and cleanup.
- **Fault scheduling**: planning a deterministic schedule from a seed and stepping
  an observed event stream to fire injections.
- **Framed-proxy throughput**: real loopback TCP forwarding through the faultable
  interposer in the no-fault case. The proxy reports 4,000 forwarded for 2,000
  sent because it forwards both directions (client->server and echoed server->client).
- **Evidence serialization/reload**: deterministic text and JSON.
- **Concurrent event processing**: the transport interposer runs two forwarder
  threads per connection; evidence recording is made thread-safe and stressed in
  the concurrency test.

The proxy throughput in the no-fault case reports 4,000 frames forwarded for 2,000
sent because the interposer forwards both directions. All measurements are
wall-clock on this machine.
