# Chaos Lab

Chaos Lab is a real systems-failure laboratory for distributed AI runtime
components. It deliberately creates failures and proves whether a target runtime
behaves correctly under them. It is not a unit-test helper, a random crash
generator, a benchmark harness, a dashboard, or a generic fuzzing wrapper.

## Systems boundary

Chaos Lab owns deliberate fault orchestration and evidence collection across
distributed AI runtime components. It makes failures reproducible across
processes, workers, coordinators, TCP connections, persistence, files,
allocations, reservations, accelerator execution, host/device transfers,
generations, epochs, boots, retries, recovery, shutdown, restart, resource
exhaustion, corrupted state, and partial operations.

Chaos Lab does not own production failure policy. The Failure Fabric defines what
failures mean and how recovery authority should be interpreted. Chaos Lab creates
failures and proves whether a target runtime behaves correctly under them.

## Core question

How should distributed AI infrastructure be subjected to deterministic,
reproducible, adversarial fault campaigns so failures, stale authority,
corruption, exhaustion, recovery behavior, and hidden assumptions are exposed
before production?

Every campaign must clearly distinguish the injected fault, any naturally
resulting secondary failure, the target-runtime response, the recovery action,
the assertion, the evidence, and the final state. Chaos Lab never pretends a
simulated hardware failure was physically induced.

## Fault model

Chaos Lab defines strongly typed fault categories (for example
PROCESS_TERMINATION, PROCESS_CRASH, PROCESS_STALL, COORDINATOR_DEATH,
WORKER_DEATH, SOCKET_CLOSE, SOCKET_RESET, SOCKET_HALF_CLOSE, CONNECTION_DROP,
FRAME_TRUNCATION, FRAME_CORRUPTION, FRAME_DUPLICATION, FRAME_REORDER, FRAME_DELAY,
STALE_EPOCH, STALE_BOOT, STALE_ATTEMPT, STALE_GENERATION, STALE_DISPATCH,
DUPLICATE_MESSAGE, CONFLICTING_DUPLICATE, PARTIAL_FILE_WRITE,
PERSISTENCE_TRUNCATION, PERSISTENCE_CORRUPTION, BAD_CHECKSUM, BAD_VERSION,
DISK_WRITE_FAILURE, RESOURCE_EXHAUSTION, HOST_MEMORY_PRESSURE,
DEVICE_MEMORY_PRESSURE, RESERVATION_EXHAUSTION, CAPACITY_REDUCTION,
CUDA_APPLICATION_FAILURE, CUDA_VERIFICATION_FAILURE, CUDA_ALLOCATION_FAILURE,
DEPENDENCY_FAILURE, RECOVERY_FAILURE, ROLLBACK_FAILURE, SHUTDOWN_RACE,
RESTART_RACE, CLOCK_SKEW_SIMULATION, INVALID_STATE_INJECTION). Fault source,
scope, severity, timing and expected effect are kept separate.

## Campaign model

A canonical ChaosCampaign carries a CampaignId, a deterministic seed, a target
set, a fault schedule, preconditions, fault actions, recovery expectations,
assertions, cleanup actions, a maximum resource envelope, a repetition count, an
evidence policy, a replay policy, a campaign generation and an optional purpose.
Campaigns are immutable once started. Named phases
(SETUP, BASELINE, ARMED, INJECTING, OBSERVING, RECOVERING, VERIFYING, CLEANUP,
COMPLETE, FAILED, ABORTED) transition through a guarded graph.

## Deterministic orchestration

Given the same campaign definition, the same deterministic seed, the same
supported environment and the same target build, the sequence of planned
injections and expected assertions is reproducible. Chaos Lab records the exact
seed, fault ordering, target versions, build mode, machine and GPU information,
process launch order, ports, authority envelopes, injection and observed
timestamps, recovery milestones and a final state digest. Deterministic
orchestration is separated from inherently nondeterministic OS timing; where
timing varies, state properties are asserted rather than fragile microsecond
timing.

## Process control

Chaos Lab provides a Windows process-control layer (CreateProcess) with process
handles, stdout/stderr capture, exit-code collection, forced kill, restart,
environment and command-line injection, temporary directories, deterministic port
allocation, child cleanup and orphan detection. TerminateProcess may only target
processes launched and owned by the campaign.

## Network fault injection

A faultable framed-TCP transport interposer supports controlled connect,
disconnect, close, half-close, reset where feasible, delay, drop, duplicate,
truncate, corrupt, inject and reorder where protocol semantics permit. Fault
actions are targeted by direction, connection, message type, sequence, worker and
campaign phase, and never corrupt unrelated system traffic. Proxy state remains
outside target-runtime internal state.

## Authority adversaries

Stale authority is one of the strongest parts of the laboratory. Chaos Lab
captures and replays old CoordinatorEpoch, old WorkerBootId, old AttemptId, old
AttemptGeneration, old DispatchId and old resource/cache/replica/accounting/recovery
generations and proves that the target rejects authority that is no longer
current. It supports deliberate combinations such as fresh epoch + stale boot,
fresh boot + stale attempt, current attempt + stale dispatch, stale generation
with an otherwise valid payload, duplicate current completion, and conflicting
duplicate completion.

## Persistence chaos

Chaos Lab supports deterministic mutation of persisted state: truncate at a byte
or record boundary, corrupt checksum/magic/version/record count, duplicate a
record, mutate an enum/identity/generation, append trailing garbage, partial
temp-file write, missing rename, stale or conflicting snapshot state. Original
files are preserved unless the campaign explicitly operates in a disposable
workspace, and destructive persistence tests always use campaign-owned copies.

## Resource chaos

Bounded resource pressure is driven within an explicit campaign cap: host
allocation pressure, reservation pressure, temporary file pressure, and handle or
socket pressure within safe laboratory bounds. Chaos Lab never intentionally
exhausts the whole workstation to instability.

## CUDA chaos

Real CUDA-backed scenarios run on an RTX 5090 / sm_120 where available: bounded
device allocation pressure, process death during GPU work, host-side verification
failure, transport ambiguity around CUDA completion, and repeated cold restart.
Chaos Lab does not claim physical GPU reset, Xid injection, ECC/PCIe/NVLink
faults, or driver crashes unless actually performed.

## Assertion engine

Typed assertions (ASSERT_ACCEPTED, ASSERT_REJECTED, ASSERT_STATE,
ASSERT_TERMINAL, ASSERT_NOT_TERMINAL, ASSERT_EXACTLY_ONE_AUTHORITY,
ASSERT_NO_STALE_MUTATION, ASSERT_NO_DOUBLE_COMMIT, ASSERT_NO_LEAK,
ASSERT_ACCOUNTING_ZERO, ASSERT_DIGEST_EQUAL, ASSERT_DIGEST_DIFFERENT,
ASSERT_RECOVERY_COMPLETE, ASSERT_PROCESS_EXIT, ASSERT_PROCESS_ALIVE,
ASSERT_RESOURCE_BASELINE) carry expected, observed, target, phase, supporting
evidence and a pass/fail result. A campaign passes only if every required
assertion passes and cleanup succeeds.

## Evidence and replay

Structured evidence is captured for every run (campaign definition, seed, target
versions, launches/exits, stdout/stderr, protocol events, injections,
assertions, state snapshots, authority envelopes, persistence digests, recovery
events, cleanup and the final result) in deterministic text and JSON. Replay
modes are PLAN_REPLAY (reproduce injections against a fresh target run),
EVIDENCE_REPLAY (recompute assertions and digests without launching targets) and
COMPARE (compare two runs). No screenshots are required.

## Safety envelope

Every destructive action operates within a declared campaign boundary: maximum
child-process count, host/device allocation caps, maximum temporary disk use,
maximum open sockets, maximum restarts, an allowed executable list, an allowed
temp-root path and an allowed PID set. TerminateProcess may only target
campaign-owned processes.

## Benchmarks

Actual completed laboratory work is measured: event ingest, assertion evaluation,
process launch/cleanup, fault scheduling, framed-proxy throughput with and
without deterministic injection, evidence serialization/reload, campaign replay
and concurrent event processing. See BENCHMARKS.md.

## Build, install and use

Requirements: C++20, CMake (>= 3.22), MSVC (/W4 /WX) and optionally a CUDA
toolchain. Configure and build with CMake, then drive campaigns through the
`chaos` CLI.

    cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=cl -DCMAKE_CUDA_COMPILER=nvcc
    cmake --build build
    ctest --test-dir build --output-on-failure
    chaos run worker-death
    chaos repeat worker-death --count 25
    chaos replay <evidence>
    chaos compare <run-a> <run-b>

Install the library/headers and exported CMake targets with `cmake --install`,
then consume via `find_package(ChaosLab)`. See EXAMPLES.md.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
