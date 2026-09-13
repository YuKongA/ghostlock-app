# Shared Execution Defaults

These values preserve pre-S03 behavior. Time units are encoded in field-name suffixes: `_ms` for milliseconds and `_us` for microseconds. S08 will wire them to Native consumers; until then they stabilize the schema and support future overrides.

## Recommended CPUs

### `recommended_cpus.main`

- Default: `0`
- Purpose: Suggested main race CPU.
- Impact: Changes scheduling, heat, and race timing; explicit UI selection takes precedence.
- Rationale: Preserves legacy fallback without topology assumptions.

### `recommended_cpus.consumer`

- Default: `1`
- Purpose: Suggested consumer CPU.
- Impact: Same-core placement increases contention; nonexistent cores fail or fall back.
- Rationale: Keeps legacy separation.

## Heap preparation

### `heap.prepare_max_attempts`

- Default: `4`
- Purpose: Full heap-preparation retry cap.
- Impact: More retries may recover transient misses but add time and heat.
- Rationale: Matches the former four-pass bound.

### `heap.prepare_timeout_ms`

- Default: `240000`
- Purpose: Overall heap preparation timeout.
- Impact: Too short misjudges slow devices; too long delays failure feedback.
- Rationale: Four minutes preserves the old bounded worst case.

### `heap.kernelsnitch_timeout_ms`

- Default: `60000`
- Purpose: KernelSnitch scan timeout.
- Impact: Affects scan completeness versus stall time.
- Rationale: Matches the prior 60-second scan window.

## Shared race timing

### `race.route_wait_ms`

- Default: `1000`
- Purpose: Shared pre-route wait.
- Impact: Changes readiness ordering.
- Rationale: Preserves the conservative one-second window.

### `race.setup_settle_us`

- Default: `50000`
- Purpose: Post-setup settle delay.
- Impact: Too short can trigger before setup completes.
- Rationale: Legacy 50 ms balance.

### `race.state_poll_interval_us`

- Default: `1000`
- Purpose: Shared state polling interval.
- Impact: Trades CPU load for reaction latency.
- Rationale: Preserves prior polling cadence.

## W1/W2/W3 stages

### `stages.w1_attempts`

- Default: `15`
- Purpose: W1 write-attempt cap.
- Impact: More attempts add heat and corruption exposure.
- Rationale: Copied from the original loop bound.

### `stages.w1_settle_us`

- Default: `100000`
- Purpose: W1 post-attempt settle.
- Impact: Controls stabilization before verification.
- Rationale: Preserves 100 ms.

### `stages.w1_scratch_repair_attempts`

- Default: `3`
- Purpose: W1 scratch-repair cap.
- Impact: More repairs may recover or compound damage.
- Rationale: Matches the former three repairs.

### `stages.w2_attempts`

- Default: `15`
- Purpose: W2 credential-write cap.
- Impact: Affects elevation probability, time, and risk.
- Rationale: Matches legacy W2.

### `stages.w2_settle_us`

- Default: `100000`
- Purpose: W2 settle delay.
- Impact: Too short may verify before visibility.
- Rationale: Preserves 100 ms.

### `stages.w3_chain_rounds`

- Default: `3`
- Purpose: Whole W3-chain rounds.
- Impact: More rounds increase coverage and exposure.
- Rationale: Legacy cap is three.

### `stages.w3_attempts`

- Default: `6`
- Purpose: W3 attempts per round.
- Impact: Changes bypass opportunity and contention.
- Rationale: Preserves six attempts.

### `stages.w3_settle_us`

- Default: `50000`
- Purpose: W3 settle delay.
- Impact: Controls verification timing and speed.
- Rationale: Preserves 50 ms.

## TCP zerocopy route

### `routes.tcp_zerocopy.attempts`

- Default: `2000`
- Purpose: TCP zerocopy probe cap.
- Impact: Major heat/time contributor; too low reduces hit rate.
- Rationale: Matches the original cap.

### `routes.tcp_zerocopy.arm_sequence`

- Default: `16`
- Purpose: Arm-sequence length.
- Impact: Changes spray/trigger ordering; wrong values miss the window.
- Rationale: Preserves the validated 16-step sequence.

### `routes.tcp_zerocopy.post_receive_hold_iterations`

- Default: `20000`
- Purpose: Post-receive hold iterations.
- Impact: Longer hold widens window but adds heat.
- Rationale: Copies the former busy-loop constant.

## Select-stack route

### `routes.select_stack.enter_delay_us`

- Default: `50000`
- Purpose: Delay before the main select trigger after consumer entry.
- Impact: Directly shifts the select-stack race window.
- Rationale: Preserves validated 50 ms timing.

### `routes.select_stack.timeout_us`

- Default: `200000`
- Purpose: Per-select timeout.
- Impact: Balances premature exit and recovery time.
- Rationale: Preserves 200 ms.

### `routes.select_stack.consumer_max_calls`

- Default: `1`
- Purpose: Consumer call cap.
- Impact: Multiple calls alter the proven layout.
- Rationale: The stable path is single-shot.

### `routes.select_stack.consumer_burst_calls`

- Default: `1`
- Purpose: Calls per consumer burst.
- Impact: Changes scheduling and stack lifetime.
- Rationale: Preserves one-call bursts.

## Multicast-waiter route

### `routes.multicast_waiter.ready_timeout_ms`

- Default: `10000`
- Purpose: Waiter-ready timeout.
- Impact: Balances slow scheduling and cleanup latency.
- Rationale: Matches legacy 10 seconds.

### `routes.multicast_waiter.post_requeue_settle_us`

- Default: `200000`
- Purpose: Post-requeue settle.
- Impact: Affects waiter-chain stabilization; shorter waits may overwrite too early.
- Rationale: Preserves 200 ms.

### `routes.multicast_waiter.post_adjust_settle_us`

- Default: `100000`
- Purpose: Post-adjust settle.
- Impact: Controls state visibility.
- Rationale: Preserves 100 ms.

## Privilege handoff

### `handoff.pre_dispatch_settle_ms`

- Default: `2000`
- Purpose: Pre-handoff settle.
- Impact: Too short may dispatch before privilege/SELinux state settles.
- Rationale: Preserves two seconds.

### `handoff.module_poll_attempts`

- Default: `30`
- Purpose: Module-state poll count.
- Impact: Together with the interval, forms the 3-second total window.
- Rationale: 30×100 ms preserves three seconds.

### `handoff.module_poll_interval_ms`

- Default: `100`
- Purpose: Module poll interval.
- Impact: Trades wakeups for latency.
- Rationale: Legacy cadence.

### `handoff.enforce_poll_attempts`

- Default: `200`
- Purpose: Enforcing-state poll count.
- Impact: Together with the interval, defines the 20-second recovery window.
- Rationale: 200×100 ms preserves twenty seconds.

### `handoff.enforce_poll_interval_ms`

- Default: `100`
- Purpose: Enforcing poll interval.
- Impact: Affects detection latency and wakeups.
- Rationale: Legacy cadence.

Every change must record device, temperature, CPU pair, success rate, and failing stage.
