# Shared Execution Defaults

These are the execution-tuning defaults every kernel shares. The resolver loads
them from `execution-tuning.conf` and `execution-<route>.conf` as presets, merges
the built-in profile, imported offsets, and user overrides, and hands the result
to native; anything a profile omits falls back to the values here.

Field-name suffixes encode units: `_ms` for milliseconds, `_us` for
microseconds. The "Impact" and "Rationale" columns explain the trade-offs rather
than prescribe values; when you do change `execution`, base it on real-device
measurements.

> Everything in this table is advisory. Only kernel geometry is required; a
> profile may omit or override these values, and the shipped default is the
> suggestion.

<table>
<thead>
<tr><th>Parent</th><th>Child</th><th>Default</th><th>Purpose</th><th>Impact</th><th>Rationale</th></tr>
</thead>
<tbody>
<tr><th rowspan="2">Recommended CPUs</th><td><code>recommended_cpus.main</code></td><td><code>0</code></td><td>Suggested main race CPU.</td><td>Changes scheduling, heat, and race timing; explicit UI selection takes precedence.</td><td>Preserves legacy fallback without topology assumptions.</td></tr>
<tr><td><code>recommended_cpus.consumer</code></td><td><code>1</code></td><td>Suggested consumer CPU.</td><td>Same-core placement increases contention; nonexistent cores fail or fall back.</td><td>Keeps legacy separation.</td></tr>
<tr><th rowspan="3">Heap preparation</th><td><code>heap.prepare_max_attempts</code></td><td><code>4</code></td><td>Full heap-preparation retry cap.</td><td>More retries may recover transient misses but add time and heat.</td><td>Matches the former four-pass bound.</td></tr>
<tr><td><code>heap.prepare_timeout_ms</code></td><td><code>240000</code></td><td>Overall heap preparation timeout.</td><td>Too short misjudges slow devices; too long delays failure feedback.</td><td>Four minutes preserves the old bounded worst case.</td></tr>
<tr><td><code>heap.kernelsnitch_timeout_ms</code></td><td><code>60000</code></td><td>KernelSnitch scan timeout.</td><td>Affects scan completeness versus stall time.</td><td>Matches the prior 60-second scan window.</td></tr>
<tr><th rowspan="4">Shared race timing</th><td><code>race.route_wait_ms</code></td><td><code>1000</code></td><td>Shared pre-route wait.</td><td>Changes readiness ordering.</td><td>Preserves the conservative one-second window.</td></tr>
<tr><td><code>race.route_done_timeout_ms</code></td><td><code>300000</code></td><td>Maximum wait for the route worker to publish its terminal status.</td><td>Expiry fail-stops the native process because PI ownership cannot be safely unwound.</td><td>Five minutes bounds a stuck route without shortening existing TCP attempt windows.</td></tr>
<tr><td><code>race.setup_settle_us</code></td><td><code>50000</code></td><td>Post-setup settle delay.</td><td>Too short can trigger before setup completes.</td><td>Legacy 50 ms balance.</td></tr>
<tr><td><code>race.state_poll_interval_us</code></td><td><code>1000</code></td><td>Shared state polling interval.</td><td>Trades CPU load for reaction latency.</td><td>Preserves prior polling cadence.</td></tr>
<tr><th rowspan="8">W1/W2/W3 stages</th><td><code>stages.w1_attempts</code></td><td><code>15</code></td><td>W1 write-attempt cap.</td><td>More attempts add heat and corruption exposure.</td><td>Copied from the original loop bound.</td></tr>
<tr><td><code>stages.w1_settle_us</code></td><td><code>100000</code></td><td>W1 post-attempt settle.</td><td>Controls stabilization before verification.</td><td>Preserves 100 ms.</td></tr>
<tr><td><code>stages.w1_scratch_repair_attempts</code></td><td><code>3</code></td><td>W1 scratch-repair cap.</td><td>More repairs may recover or compound damage.</td><td>Matches the former three repairs.</td></tr>
<tr><td><code>stages.w2_attempts</code></td><td><code>15</code></td><td>W2 credential-write cap.</td><td>Affects elevation probability, time, and risk.</td><td>Matches legacy W2.</td></tr>
<tr><td><code>stages.w2_settle_us</code></td><td><code>100000</code></td><td>W2 settle delay.</td><td>Too short may verify before visibility.</td><td>Preserves 100 ms.</td></tr>
<tr><td><code>stages.w3_chain_rounds</code></td><td><code>3</code></td><td>Whole W3-chain rounds.</td><td>More rounds increase coverage and exposure.</td><td>Legacy cap is three.</td></tr>
<tr><td><code>stages.w3_attempts</code></td><td><code>6</code></td><td>W3 attempts per round.</td><td>Changes bypass opportunity and contention.</td><td>Preserves six attempts.</td></tr>
<tr><td><code>stages.w3_settle_us</code></td><td><code>50000</code></td><td>W3 settle delay.</td><td>Controls verification timing and speed.</td><td>Preserves 50 ms.</td></tr>
<tr><th rowspan="3">TCP zerocopy route</th><td><code>routes.tcp_zerocopy.attempts</code></td><td><code>2000</code></td><td>TCP zerocopy probe cap.</td><td>Major heat/time contributor; too low reduces hit rate.</td><td>Matches the original cap.</td></tr>
<tr><td><code>routes.tcp_zerocopy.arm_sequence</code></td><td><code>16</code></td><td>Arm-sequence length.</td><td>Changes spray/trigger ordering; wrong values miss the window.</td><td>Preserves the validated 16-step sequence.</td></tr>
<tr><td><code>routes.tcp_zerocopy.post_receive_hold_iterations</code></td><td><code>20000</code></td><td>Post-receive hold iterations.</td><td>Longer hold widens window but adds heat.</td><td>Copies the former busy-loop constant.</td></tr>
<tr><th rowspan="4">Select-stack route</th><td><code>routes.select_stack.enter_delay_us</code></td><td><code>50000</code></td><td>Delay before the main select trigger after consumer entry.</td><td>Directly shifts the select-stack race window.</td><td>Preserves validated 50 ms timing.</td></tr>
<tr><td><code>routes.select_stack.timeout_us</code></td><td><code>200000</code></td><td>Per-select timeout.</td><td>Balances premature exit and recovery time.</td><td>Preserves 200 ms.</td></tr>
<tr><td><code>routes.select_stack.consumer_max_calls</code></td><td><code>1</code></td><td>Consumer call cap.</td><td>Multiple calls alter the proven layout.</td><td>The stable path is single-shot.</td></tr>
<tr><td><code>routes.select_stack.consumer_burst_calls</code></td><td><code>1</code></td><td>Calls per consumer burst.</td><td>Changes scheduling and stack lifetime.</td><td>Preserves one-call bursts.</td></tr>
<tr><th rowspan="3">Multicast-waiter route</th><td><code>routes.multicast_waiter.ready_timeout_ms</code></td><td><code>10000</code></td><td>Waiter-ready timeout.</td><td>Balances slow scheduling and cleanup latency.</td><td>Matches legacy 10 seconds.</td></tr>
<tr><td><code>routes.multicast_waiter.post_requeue_settle_us</code></td><td><code>200000</code></td><td>Post-requeue settle.</td><td>Affects waiter-chain stabilization; shorter waits may overwrite too early.</td><td>Preserves 200 ms.</td></tr>
<tr><td><code>routes.multicast_waiter.post_adjust_settle_us</code></td><td><code>100000</code></td><td>Post-adjust settle.</td><td>Controls state visibility.</td><td>Preserves 100 ms.</td></tr>
<tr><th rowspan="5">Privilege handoff</th><td><code>handoff.pre_dispatch_settle_ms</code></td><td><code>2000</code></td><td>Pre-handoff settle.</td><td>Too short may dispatch before privilege/SELinux state settles.</td><td>Preserves two seconds.</td></tr>
<tr><td><code>handoff.module_poll_attempts</code></td><td><code>30</code></td><td>Module-state poll count.</td><td>Together with the interval, forms the 3-second total window.</td><td>30×100 ms preserves three seconds.</td></tr>
<tr><td><code>handoff.module_poll_interval_ms</code></td><td><code>100</code></td><td>Module poll interval.</td><td>Trades wakeups for latency.</td><td>Legacy cadence.</td></tr>
<tr><td><code>handoff.enforce_poll_attempts</code></td><td><code>200</code></td><td>Enforcing-state poll count.</td><td>Together with the interval, defines the 20-second recovery window.</td><td>200×100 ms preserves twenty seconds.</td></tr>
<tr><td><code>handoff.enforce_poll_interval_ms</code></td><td><code>100</code></td><td>Enforcing poll interval.</td><td>Affects detection latency and wakeups.</td><td>Legacy cadence.</td></tr>
</tbody>
</table>
