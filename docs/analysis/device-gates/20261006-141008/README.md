# Device gate inconclusive - device dropped mid-run

release: 5.15.189-android13-8-00016-g51bba4309aac-ab14546557
binary_sha256: 5731450a9030...
verdict: inconclusive - native exit=unknown, the device disappeared from adb during the run

Same class as the two runs in 20261006-1331{50,46}: the KERNEL-PANIC-01 environment/timing
behaviour where the same binary passes and fails on different boots. The device came back and
a later cold run passed (20261006-141317).