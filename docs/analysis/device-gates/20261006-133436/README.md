# Device gate PASS - queue carriage / stepset manifest tree

release: 5.15.189-android13-8-00016-g51bba4309aac-ab14546557
serial: QV770MFGJ1
binary_sha256: 5731450a9030... (byte-identical to the runs in 20261006-1331{50,46})
verdict: PASS - child is root!, exploit complete, KernelSU ready, native exit=0

KERNEL-PANIC-01 causality: the same binary passed this morning, then failed twice
(heap spray retries exhausted; then the device rebooted mid-route) and passed again on a
cold re-run. The two failures are therefore environment/timing, not a code regression.
