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

## Completing modules

- **Resource accounting** (resource.h / resource.cpp): a ResourceBaseline /
  ResourceDelta tracker records before/peak/after counts for owned child
  processes, sockets, host and device bytes, temp files and threads, and issues a
  clean/leak verdict. The SafetyEnvelope tracks peak child/socket counts.
- **Assertion evaluator** (assertions.h / assertions.cpp): every ASSERT_* kind is
  executable via evaluate_assertion(AssertionSpec, RunFacts, phase), producing a
  deterministic AssertionResult (expected/observed/target/phase/evidence/pass).
- **Coordinator death**: the worker reconnects and re-registers on coordinator
  loss; a dedicated coordinator-death campaign terminates coordinator A as a real
  process, launches coordinator B at an advanced epoch, proves stale authority is
  rejected and fresh work succeeds under one authority domain.
- **Restart storms**: bounded worker, coordinator and alternating worker storms
  with fresh WorkerBootId each iteration, stale-boot rejection and no leak.
- **State races**: deterministic adversarial interleavings (COMPLETE vs death,
  cancel/retry vs stale completion) that assert invariants (no double commit, no
  stale mutation, no leak, no split authority) over whichever legal outcome
  occurred.
- **CUDA scenarios**: allocation pressure (A), process death during GPU work (B),
  host verification failure with retry and CPU parity (C), transport ambiguity
  around a dropped completion via the real interposer (D), and 25x cold restart
  with exact device-memory baseline return (E).
- **Transport coverage**: drop, corrupt, truncate, duplicate, delay, close and
  reconnect are all exercised on real loopback TCP through the interposer.
- **Persistence during recovery**: save, kill the owning process, reload in a
  fresh process, and prove recovery generation is monotonic and stale ownership
  does not resurrect.
- **Concurrency**: EvidenceRecorder is thread-safe and stressed under high
  contention; proxy forwarders are single-writer per connection.

## Physical boundaries

Chaos Lab does not fabricate physical hardware faults. It never claims GPU device
reset, Xid injection, ECC, PCIe or NVLink physical failure, thermal faults, or real
multi-GPU behaviour unless actually performed. Those remain genuine
physical/environmental boundaries, distinct from software closure.
