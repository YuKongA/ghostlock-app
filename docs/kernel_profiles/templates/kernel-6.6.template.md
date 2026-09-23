# 6.6 Kernel Profile Template Guide

Template file: [kernel-6.6.template.conf](kernel-6.6.template.conf). It lists every field in the template with its meaning and can be read on its own.

> Required fields stay `null` until you fill them in, so the template remains intentionally invalid and your editor flags unfinished adaptations immediately. Don't replace `null` with `0` just to silence validation.

<table>
<thead>
<tr><th>Parent</th><th>Child</th><th>Default</th><th>Purpose</th><th>Impact</th><th>Rationale</th></tr>
</thead>
<tbody>
<tr><th rowspan="3">Identity and route selection</th><td><code>schema_version</code></td><td><code>1</code></td><td>Profile schema version; always 1. Gates future format changes.</td><td rowspan="3">An incorrect value can change target selection, structure bounds, or race timing, causing failure, deadlock, memory corruption, a black screen, or reboot.</td><td rowspan="3">This preserves the verified 6.6 baseline but is not a stable ABI; revalidate it against the same target image.</td></tr>
<tr><td><code>release</code></td><td></td><td>Kernel version string exactly as <code>uname -r</code> reports it; used for both the file name and matching.</td></tr>
<tr><td><code>kernel_major</code></td><td><code>6</code></td><td>Kernel major version (5 or 6); drives address resolution and sanity checks.</td></tr>
<tr><th rowspan="15">Task structure offsets</th><td><code>task_struct.prio</code></td><td></td><td>Offset of <code>task_struct.prio</code>, read by the PI priority-boost check.</td><td rowspan="15">An incorrect value can change target selection, structure bounds, or race timing, causing failure, deadlock, memory corruption, a black screen, or reboot.</td><td rowspan="15">This preserves the verified 6.6 baseline but is not a stable ABI; revalidate it against the same target image.</td></tr>
<tr><td><code>task_struct.normal_prio</code></td><td></td><td>Offset of the normal scheduling priority.</td></tr>
<tr><td><code>task_struct.sched_task_group</code></td><td></td><td>Offset of the <code>sched_task_group</code> pointer.</td></tr>
<tr><td><code>task_struct.pi_lock</code></td><td></td><td>Offset of the <code>pi_lock</code> spinlock.</td></tr>
<tr><td><code>task_struct.pi_waiters</code></td><td></td><td>Offset of the PI waiters list.</td></tr>
<tr><td><code>task_struct.pi_top_task</code></td><td></td><td>Offset of the top-task pointer.</td></tr>
<tr><td><code>task_struct.pi_blocked_on</code></td><td></td><td>Offset of the pointer to the waiter's current blocking object.</td></tr>
<tr><td><code>task_struct.pid</code></td><td></td><td>Offset of the process PID field.</td></tr>
<tr><td><code>task_struct.tgid</code></td><td></td><td>Offset of the thread-group ID field.</td></tr>
<tr><td><code>task_struct.atomic_flags</code></td><td></td><td>Offset of <code>atomic_flags</code>, used to clear state markers.</td></tr>
<tr><td><code>task_struct.real_cred</code></td><td></td><td>Offset of the <code>real_cred</code> pointer.</td></tr>
<tr><td><code>task_struct.cred</code></td><td></td><td>Offset of the <code>cred</code> pointer.</td></tr>
<tr><td><code>task_struct.comm</code></td><td></td><td>Offset of <code>comm</code>; the leaf-direction probe reads it.</td></tr>
<tr><td><code>task_struct.tasks</code></td><td></td><td>Offset of the task list <code>tasks</code>.</td></tr>
<tr><td><code>task_struct.seccomp</code></td><td></td><td>Offset of <code>seccomp</code>; W3 zeroes its <code>mode</code>.</td></tr>
<tr><th rowspan="5">Credential layout</th><td><code>cred.copy_size</code></td><td><code>136</code></td><td>Bytes copied when forging a credential.</td><td rowspan="5">An incorrect value can change target selection, structure bounds, or race timing, causing failure, deadlock, memory corruption, a black screen, or reboot.</td><td rowspan="5">This preserves the verified 6.6 baseline but is not a stable ABI; revalidate it against the same target image.</td></tr>
<tr><td><code>cred.usage_value</code></td><td><code>1</code></td><td>Target value for the <code>usage</code> refcount.</td></tr>
<tr><td><code>cred.caps_offset</code></td><td><code>48</code></td><td>Offset of the capability set inside <code>cred</code>.</td></tr>
<tr><td><code>cred.caps_count</code></td><td><code>5</code></td><td>Number of capabilities <code>cred</code> carries.</td></tr>
<tr><td><code>cred.caps_value</code></td><td><code>-1</code></td><td>Value used to fill the capability bitmap.</td></tr>
<tr><th rowspan="2">Waiter, heap, and route geometry</th><td><code>kernelsnitch.collisions</code></td><td><code>4</code></td><td>Number of futex collisions KernelSnitch needs.</td><td rowspan="2">An incorrect value can change target selection, structure bounds, or race timing, causing failure, deadlock, memory corruption, a black screen, or reboot.</td><td rowspan="2">This preserves the verified 6.6 baseline but is not a stable ABI; revalidate it against the same target image.</td></tr>
<tr><td><code>route.select_stack.waiter_shift</code></td><td></td><td>Relative shift of the waiter on the select stack (0 is valid; negative is common). When select is the fallback, set it under <code>fallback.route.select_stack</code>.</td></tr>
<tr><th rowspan="9">Kernel-relative symbols</th><td><code>offset.init_task</code></td><td></td><td>Offset of <code>init_task</code> relative to the kernel image base.</td><td rowspan="9">An incorrect value can change target selection, structure bounds, or race timing, causing failure, deadlock, memory corruption, a black screen, or reboot.</td><td rowspan="9">This preserves the verified 6.6 baseline but is not a stable ABI; revalidate it against the same target image.</td></tr>
<tr><td><code>offset.init_cred</code></td><td></td><td>Offset of <code>init_cred</code>; W2 repair writes its +8.</td></tr>
<tr><td><code>offset.root_task_group</code></td><td></td><td>Offset of <code>root_task_group</code>.</td></tr>
<tr><td><code>offset.selinux_enforcing</code></td><td></td><td>Offset of <code>selinux_state.enforcing</code>; W1 writes 0.</td></tr>
<tr><td><code>offset.selinux_blob_sizes</code></td><td></td><td>Offset of <code>selinux_blob_sizes</code>.</td></tr>
<tr><td><code>offset.security_hook_heads</code></td><td></td><td>Offset of <code>security_hook_heads</code>.</td></tr>
<tr><td><code>offset.slide_nfulnl_logger</code></td><td></td><td>KASLR slide anchor <code>nfulnl_logger</code>.</td></tr>
<tr><td><code>offset.slide_boot_id</code></td><td></td><td>KASLR slide anchor (boot id data).</td></tr>
<tr><td><code>offset.slide_loggers_0_1</code></td><td></td><td>KASLR slide anchor <code>loggers[0..1]</code>.</td></tr>
<tr><th rowspan="2">Recommended CPUs</th><td><code>execution.recommended_cpus.main</code></td><td><code>0</code></td><td>Suggested main race CPU.</td><td>Changes scheduling, heat, and race timing; explicit UI selection takes precedence.</td><td>Preserves legacy fallback without topology assumptions.</td></tr>
<tr><td><code>execution.recommended_cpus.consumer</code></td><td><code>1</code></td><td>Suggested consumer CPU.</td><td>Same-core placement increases contention; nonexistent cores fail or fall back.</td><td>Keeps legacy separation.</td></tr>
<tr><th rowspan="3">Heap preparation</th><td><code>execution.heap.prepare_max_attempts</code></td><td><code>4</code></td><td>Full heap-preparation retry cap.</td><td>More retries may recover transient misses but add time and heat.</td><td>Matches the former four-pass bound.</td></tr>
<tr><td><code>execution.heap.prepare_timeout_ms</code></td><td><code>240000</code></td><td>Overall heap preparation timeout.</td><td>Too short misjudges slow devices; too long delays failure feedback.</td><td>Four minutes preserves the old bounded worst case.</td></tr>
<tr><td><code>execution.heap.kernelsnitch_timeout_ms</code></td><td><code>60000</code></td><td>KernelSnitch scan timeout.</td><td>Affects scan completeness versus stall time.</td><td>Matches the prior 60-second scan window.</td></tr>
<tr><th rowspan="3">Shared race timing</th><td><code>execution.race.route_wait_ms</code></td><td><code>1000</code></td><td>Shared pre-route wait.</td><td>Changes readiness ordering.</td><td>Preserves the conservative one-second window.</td></tr>
<tr><td><code>execution.race.setup_settle_us</code></td><td><code>50000</code></td><td>Post-setup settle delay.</td><td>Too short can trigger before setup completes.</td><td>Legacy 50 ms balance.</td></tr>
<tr><td><code>execution.race.state_poll_interval_us</code></td><td><code>1000</code></td><td>Shared state polling interval.</td><td>Trades CPU load for reaction latency.</td><td>Preserves prior polling cadence.</td></tr>
<tr><th rowspan="8">W1/W2/W3 stages</th><td><code>execution.stages.w1_attempts</code></td><td><code>15</code></td><td>W1 write-attempt cap.</td><td>More attempts add heat and corruption exposure.</td><td>Copied from the original loop bound.</td></tr>
<tr><td><code>execution.stages.w1_settle_us</code></td><td><code>100000</code></td><td>W1 post-attempt settle.</td><td>Controls stabilization before verification.</td><td>Preserves 100 ms.</td></tr>
<tr><td><code>execution.stages.w1_scratch_repair_attempts</code></td><td><code>3</code></td><td>W1 scratch-repair cap.</td><td>More repairs may recover or compound damage.</td><td>Matches the former three repairs.</td></tr>
<tr><td><code>execution.stages.w2_attempts</code></td><td><code>15</code></td><td>W2 credential-write cap.</td><td>Affects elevation probability, time, and risk.</td><td>Matches legacy W2.</td></tr>
<tr><td><code>execution.stages.w2_settle_us</code></td><td><code>100000</code></td><td>W2 settle delay.</td><td>Too short may verify before visibility.</td><td>Preserves 100 ms.</td></tr>
<tr><td><code>execution.stages.w3_chain_rounds</code></td><td><code>3</code></td><td>Whole W3-chain rounds.</td><td>More rounds increase coverage and exposure.</td><td>Legacy cap is three.</td></tr>
<tr><td><code>execution.stages.w3_attempts</code></td><td><code>6</code></td><td>W3 attempts per round.</td><td>Changes bypass opportunity and contention.</td><td>Preserves six attempts.</td></tr>
<tr><td><code>execution.stages.w3_settle_us</code></td><td><code>50000</code></td><td>W3 settle delay.</td><td>Controls verification timing and speed.</td><td>Preserves 50 ms.</td></tr>
<tr><th rowspan="3">TCP zerocopy route</th><td><code>execution.routes.tcp_zerocopy.attempts</code></td><td><code>2000</code></td><td>TCP zerocopy probe cap.</td><td>Major heat/time contributor; too low reduces hit rate.</td><td>Matches the original cap.</td></tr>
<tr><td><code>execution.routes.tcp_zerocopy.arm_sequence</code></td><td><code>16</code></td><td>Arm-sequence length.</td><td>Changes spray/trigger ordering; wrong values miss the window.</td><td>Preserves the validated 16-step sequence.</td></tr>
<tr><td><code>execution.routes.tcp_zerocopy.post_receive_hold_iterations</code></td><td><code>20000</code></td><td>Post-receive hold iterations.</td><td>Longer hold widens window but adds heat.</td><td>Copies the former busy-loop constant.</td></tr>
<tr><th rowspan="4">Select-stack route</th><td><code>execution.routes.select_stack.enter_delay_us</code></td><td><code>50000</code></td><td>Delay before the main select trigger after consumer entry.</td><td>Directly shifts the select-stack race window.</td><td>Preserves validated 50 ms timing.</td></tr>
<tr><td><code>execution.routes.select_stack.timeout_us</code></td><td><code>200000</code></td><td>Per-select timeout.</td><td>Balances premature exit and recovery time.</td><td>Preserves 200 ms.</td></tr>
<tr><td><code>execution.routes.select_stack.consumer_max_calls</code></td><td><code>1</code></td><td>Consumer call cap.</td><td>Multiple calls alter the proven layout.</td><td>The stable path is single-shot.</td></tr>
<tr><td><code>execution.routes.select_stack.consumer_burst_calls</code></td><td><code>1</code></td><td>Calls per consumer burst.</td><td>Changes scheduling and stack lifetime.</td><td>Preserves one-call bursts.</td></tr>
<tr><th rowspan="3">Multicast-waiter route</th><td><code>execution.routes.multicast_waiter.ready_timeout_ms</code></td><td><code>10000</code></td><td>Waiter-ready timeout.</td><td>Balances slow scheduling and cleanup latency.</td><td>Matches legacy 10 seconds.</td></tr>
<tr><td><code>execution.routes.multicast_waiter.post_requeue_settle_us</code></td><td><code>200000</code></td><td>Post-requeue settle.</td><td>Affects waiter-chain stabilization; shorter waits may overwrite too early.</td><td>Preserves 200 ms.</td></tr>
<tr><td><code>execution.routes.multicast_waiter.post_adjust_settle_us</code></td><td><code>100000</code></td><td>Post-adjust settle.</td><td>Controls state visibility.</td><td>Preserves 100 ms.</td></tr>
<tr><th rowspan="5">Privilege handoff</th><td><code>execution.handoff.pre_dispatch_settle_ms</code></td><td><code>2000</code></td><td>Pre-handoff settle.</td><td>Too short may dispatch before privilege/SELinux state settles.</td><td>Preserves two seconds.</td></tr>
<tr><td><code>execution.handoff.module_poll_attempts</code></td><td><code>30</code></td><td>Module-state poll count.</td><td>Together with the interval, forms the 3-second total window.</td><td>30×100 ms preserves three seconds.</td></tr>
<tr><td><code>execution.handoff.module_poll_interval_ms</code></td><td><code>100</code></td><td>Module poll interval.</td><td>Trades wakeups for latency.</td><td>Legacy cadence.</td></tr>
<tr><td><code>execution.handoff.enforce_poll_attempts</code></td><td><code>200</code></td><td>Enforcing-state poll count.</td><td>Together with the interval, defines the 20-second recovery window.</td><td>200×100 ms preserves twenty seconds.</td></tr>
<tr><td><code>execution.handoff.enforce_poll_interval_ms</code></td><td><code>100</code></td><td>Enforcing poll interval.</td><td>Affects detection latency and wakeups.</td><td>Legacy cadence.</td></tr>
</tbody>
</table>
