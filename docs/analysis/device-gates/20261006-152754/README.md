# Device gate PASS - M5 (the combination token is gone from the wire)

release: 5.15.189-android13-8-00016-g51bba4309aac-ab14546557
serial: QV770MFGJ1
binary_sha256: 24e9accf84b9... (rebuilt with the M5 changes)
verdict: PASS - child is root, exploit complete, KernelSU ready, native exit=0

This is the design section 4.4 acceptance for M5 on the 43499 path: the migrated profiles declare route plus queue and carry no steps token, native refuses a token value with a named plan_error instead of materialising one, and the attack path still runs to completion.

The 43284 half of the M5 acceptance is covered by the native unit tests re-pointed at the new semantics (a 43284 token negative case plus the eight renamed combination-token cases); this device's release profile drives the 43499 backend, so a 43499 cold boot is the meaningful end-to-end run here.

Still open (not part of this gate): the two M5 falsifications of the Kotlin guards and the explicit three-bucket count.