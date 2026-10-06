# Device gate PASS - M3 queue-only profiles

release: 5.15.189-android13-8-00016-g51bba4309aac-ab14546557
serial: QV770MFGJ1
binary_sha256: 5731450a9030...
verdict: PASS - child is root!, exploit complete, KernelSU ready, native exit=0

This run is the hard acceptance for M3: the migrated profiles declare route plus queue and
no longer carry the steps token, so the native plan no longer reports
queue-and-token-both-present and the attack path runs to completion.
Earlier attempts are archived in 20261006-1331{50,46} (KERNEL-PANIC-01 class) and
20261006-140744 (the real token conflict) and 20261006-141008 (device dropped).
