# Examples

All examples use the real built binaries and real OS processes. Build first, then
run from the build output directory so the target binaries resolve.

    cmake --build build
    cd build

## Worker kill and restart

    chaos multiprocess --count 5

## Coordinator death and restart

The multiprocess closure proof launches a real coordinator, kills a worker while
the authority is live, restarts it with a fresh boot, and verifies recovery.

## Stale-authority replay

The multiprocess closure proof replays a stale boot, stale epoch, stale attempt,
stale generation, a duplicate old completion and a conflicting duplicate, and
proves all are rejected while fresh work succeeds.

    chaos multiprocess --count 5

## Transport corruption

The framed-TCP interposer drops, corrupts and truncates frames on loopback TCP.

    ctest -R test_transport

## Duplicate / conflicting duplicate

Covered by the authority adversarial section of the multiprocess closure proof.

## Persistence truncation and partial write

    ctest -R test_persistence

## Resource pressure (bounded)

    ctest -R test_resource

## CUDA worker death on an RTX 5090

    chaos multiprocess --cuda --count 1

## Randomized fixed-seed campaign

    ctest -R test_fuzz

## Evidence replay and comparison

Runs record evidence into the evidence/ directory per run. Replay and comparison
are exercised by the evidence/replay unit tests:

    ctest -R test_replay

## Repeat

    chaos multiprocess --count 25

## Coordinator death and failover

    chaos coordinator-death --count 3

## Restart storms

    chaos storm-worker --count 25
    chaos storm-coordinator --count 10

## State races

    chaos race complete-vs-death

## Expanded transport chaos

    chaos transport

## Persistence during active recovery

    chaos persistence-recovery

## Randomized / fixed-seed campaigns

    chaos fuzz 0xABCD

## Evidence replay closure

    chaos compare-runs

## Assertion evaluator

    chaos assert

## CUDA scenarios

    chaos cuda-verify   # verification failure (C)
    chaos cuda-transport # transport ambiguity (D)
    chaos cuda-restart --count 25 # repeated cold restart (E)
