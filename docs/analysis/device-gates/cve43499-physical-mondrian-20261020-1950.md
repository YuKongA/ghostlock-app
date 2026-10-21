# Physical device route results (mondrian)

- Device: 23013RK75C / mondrian, Android 16, kernel 5.10.252-gki-gabcbe4b2bc10
- adb root enabled (`adb root`, uid=0)
- Extractor: boot_b pulled, /proc/kallsyms and /proc/iomem used; profile remains incomplete (no BTF), `available=[mcast_rootchild]`.

## select_stack

`NativeSelectStackInstrumentedTest` PASS on the physical device:

`statusCode=0, statusStep=0, attempts=1, calls=1, successes=1, ownerRequeueResult=1, ownerRequeueErrno=0, waiterParkElapsedUs=2000135`

The same absolute-timeout + `val3=*wait` + owner-requeue fix works on the 5.10 physical kernel. This is route-success evidence only: target 0x1000 is not a verified security target; no uid/page-landing claim.

## tcp_zerocopy

`NativeTcpZerocopyInstrumentedTest` (with prepared fake page) on the same device:

`status=2, step=45, errno=0, userspaceClean=1, kernelDisarmed=1`

Step 45 means the route ran its attempts and did not win. No crash. This is not success.

After adding TCP diagnostics: `consumerCalls=0, consumerSuccesses=0, getsockoptRet=-1, getsockoptErrno=92 (ENOPROTOOPT), zcCopies=0`. The device kernel rejects `TCP_ZEROCOPY_RECEIVE` (opt 61) with ENOPROTOOPT, so this TCP route cannot reach its copy path on this device. This is a kernel-configuration blocker, not a Kotlin/JNI wiring failure.

## Notes

- Extractor on 5.10 without BTF cannot derive select/TCP geometry; nevertheless select route reached success with the current config.
- Do not treat step=0 as uid=0.

## perf task candidate

A standalone root `perf_event_open` probe with `PERF_SAMPLE_REGS_INTR` delivered samples on this 5.10 kernel; `reg[10]` was the stable direct-map candidate `ffffff80ffffffd0`. The same probe inside the app process returned 0, so an unprivileged app cannot use this sampling path here. The root-sampled candidate was not yet bound to a route target.

Under `setenforce 0`, the app's own perf probe returned the same candidate `ffffff80ffffffd0`. A select run with target `candidate + 0x798` and value `0xffffff80aa66eef0` returned `statusStep=0, successes=1` but `uid-after=10257` (unchanged). A second run targeting `candidate + 0x7a8` (comm) left `/proc/self/comm` unchanged (`m.ghostlock.app`). This is decisive: the select route reports success but the write does not land at the sampled task. A scan of candidate+0x7a0/0x7a8/0x7b0 also left `/proc/self/comm` unchanged, so the failure is not a single comm offset; the writer/target pair is still wrong.
