# Architecture

Chaos Lab is C++20, Windows-first, CMake-built. Source is split into a
CUDA-independent core library, an OS/process/network layer, an optional CUDA
layer, and the CLI plus target runtime apps.

## Layers

- **chaoslab** (core, no OS/CUDA dependency). Deterministic foundations: SHA-256
  digests, seed-driven PRNG, strongly typed identities, the fault model, the
  canonical campaign model, the typed scenario DSL/builder, the deterministic
  fault scheduler, the assertion engine, evidence recording/serialization, the
  safety envelope, replay/compare, and the fixed-seed fuzzer and campaign reducer.
- **chaoslab_os** (Windows-first process/OS layer). Process launch/kill/restart,
  the framed-TCP transport interposer and socket helpers, bounded host/resource
  pressure, deterministic persistence mutation, the authority adversary model,
  the target runtime framed protocol, and Windows error formatting.
- **chaoslab_cuda** (optional). Real device-buffer allocation, host/device copies
  and a real bounded SAXPY kernel compiled for sm_120, guarded by a device check.
- **chaos_cli** (the chaos controller). It launches coordinator and worker target
  processes, scripts the fault schedule, injects faults (process termination,
  transport faults, persistence corruption, authority replay, resource pressure,
  CUDA), records evidence, evaluates assertions and cleans up.
- **target_coordinator / target_worker**. The authority-aware target runtime used
  by the multiprocess closure proof and the CUDA worker-death scenario.

## Include layout

The include/chaoslab directory is the public, installable interface. Mutating
state lives behind the public API only; the safety envelope and the evidence
recorder are the two choke points through which all destructive actions and
observations flow.

## Determinism

All randomness derives from a campaign seed. Identities serialize deterministically
and round-trip exactly. Evidence serializes to deterministic text and JSON and
produces a stable digest. The scheduler plans injections from the seed and fires
them on observed event/state/message barriers rather than arbitrary sleeps where
possible.

## Concurrency model

Chaos Lab's controller avoids network I/O and blocking process waits while holding
the global campaign-state mutex. The transport interposer uses two per-connection
forwarder threads (one per direction) that only exchange state through the frame
stream and a per-connection stop flag; faults are snapshotted per connection.
Process handles, sockets and allocations are tracked by the safety envelope so
cleanup is idempotent and every owned resource is reaped.

## Boundaries

Destructive actions are bounded by the resource envelope (allowed executable list,
allowed temp root, allowed PID set, process/socket/restart/disk caps). The
controller only ever kills processes it launched. Persistence mutation operates on
campaign-owned copies. Proxy state never enters the target runtime's internal
state.
