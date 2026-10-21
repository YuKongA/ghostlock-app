# CVE-2026-43499 select_stack route — AVD success

- Time: 2026-10-20 19:10 America/Toronto
- Device: GhostLock_6_6 (Pixel_5 AVD) - Android 15, kernel 6.6.30
- Test: com.ghostlock.app.NativeSelectStackInstrumentedTest
- Result: PASS; `statusStep=0`, `successes=1`

## What fixed it

1. Futex wait timeout was relative; it must be an absolute CLOCK_MONOTONIC deadline. Before this, the waiter returned in ~33 us with a stale ETIMEDOUT.
2. `FUTEX_CMP_REQUEUE_PI` `val3` is the expected value at `uaddr` (the wait word), not at the target. Using `val3=*target` produced EAGAIN; using `val3=*wait` (0) worked.
3. The requeue is executed by the owner thread while it holds the target and after `waiter_waiting` + 50 ms.

## Observed result

`NativeSelectStackResult(statusCode=0, statusStep=0, attempts=1, calls=1, successes=1, waiterLock=0, waiterPark=-1, waiterParkErrno=110, consumerSetattr=0, consumerSetattrErrno=0, requeueResult=0, requeueErrno=25, raceLock=0, raceRequeue=0, raceRequeueErrno=25, raceOutcome=5, targetOwnerWord=<non-zero>, waitWordValue=0, waiterParkElapsedUs=2001167, ownerRequeueResult=1, ownerRequeueErrno=0)`

The key evidence is `ownerRequeueResult=1` and `successes=1`.

## What this proves

- select_stack route can reach its successful PI requeue on the 6.6 AVD;
- the route's actor/requeue ordering is now correct;
- JNI/result mapping confirms statusStep=0.

## What this does NOT prove

- a kernel page landing/readback;
- uid=0;
- the selected target was a real security-relevant address;
- physical-device reliability.
