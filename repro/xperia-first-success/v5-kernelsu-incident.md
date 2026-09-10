# V5 KernelSU incident

V5 reached W1 and W2 on their first attempts, loaded KernelSU, restored
SELinux enforcing, and provided a working `u:r:ksu:s0` shell. Immediately
after `load_policy` and KernelSU Manager startup, Android processes began
crashing with invalid ART references whose values matched the low 32 bits of
the W1 target (`selinux_state`, `0x2adaad88`). `system_server` eventually died
while the kernel, ADB, KernelSU module, and root shell remained alive.

The one-shot V3 payload used a reclaimed payload address as the promoted
rb-tree child. The erase primitive writes the destination back into that
child, leaving `selinux_state - 8` in memory that can later be reused. The
`main` branch already contains the corresponding fix: use the stable
`empty_zero_page` child for Xperia 5.15 W1. V6 combines that fix with immediate
post-W2 root dispatch and does not enable the experimental resident writer.

Full logcat and tombstones were saved locally under `runlogs/evidence/` and
are intentionally excluded from Git because they contain device data.
