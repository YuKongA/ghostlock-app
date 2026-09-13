# 公共执行默认值 / Shared Execution Defaults

这些值保持 S03 迁移前行为。时间字段的单位由后缀决定：`_ms` 为毫秒，`_us` 为微秒。These values preserve pre-S03 behavior; units are encoded in field names. S08 才会把它们接到 Native 调用点；在此之前它们用于稳定 schema 和未来覆盖。S08 will wire them to Native consumers.

| JSON 路径 / Path | 默认值 | 作用 / Purpose | 影响 / Impact | 默认理由 / Rationale |
|---|---:|---|---|---|
| `recommended_cpus.main` | 0 | 推荐主竞态线程核心。Suggested main race CPU. | 改变调度、热量与竞态窗口；UI 显式选择优先。Changes scheduling, heat, and race timing. | 兼容旧的 0/1 回退且不假设设备拓扑。Preserves legacy fallback without topology assumptions. |
| `recommended_cpus.consumer` | 1 | 推荐 consumer 核心。Suggested consumer CPU. | 与 main 同核会增加互相抢占；不存在的核心会失败或回退。Same-core placement increases contention. | 与 main 分离并保持旧默认。Keeps legacy separation. |
| `heap.prepare_max_attempts` | 4 | heap 准备的完整重试上限。Full heap-preparation retry cap. | 增大可提高偶发恢复率，也增加耗时和温升。More retries may recover transient misses but add heat. | 原实现最多四轮，迁移时保持行为。Matches the former four-pass bound. |
| `heap.prepare_timeout_ms` | 240000 | 单次 heap 准备总超时。Overall heap preparation timeout. | 太短会误判慢设备；太长会延迟失败反馈。Controls slow-device tolerance. | 4 分钟覆盖旧最坏路径且仍有上界。Four minutes preserves the old bounded worst case. |
| `heap.kernelsnitch_timeout_ms` | 60000 | KernelSnitch 扫描等待上限。KernelSnitch scan timeout. | 缩短可能漏掉结果，增大会延长卡住状态。Affects scan completeness versus stall time. | 原扫描窗口为 60 秒。Matches the prior scan window. |
| `race.route_wait_ms` | 1000 | 路线启动前共享等待。Shared pre-route wait. | 影响线程/对象就绪顺序。Changes readiness ordering. | 1 秒是旧实现的保守稳定窗口。Preserves the conservative one-second window. |
| `race.setup_settle_us` | 50000 | 竞态准备后的稳定等待。Post-setup settle delay. | 太短会在资源未就绪时触发。Too short can trigger before setup completes. | 原 50 ms 平衡可靠性和速度。Legacy 50 ms balance. |
| `race.state_poll_interval_us` | 1000 | 共享状态轮询间隔。Shared state polling interval. | 越小 CPU 占用越高，越大响应越迟。Trades CPU load for reaction latency. | 1 ms 保持旧轮询粒度。Preserves prior polling cadence. |
| `stages.w1_attempts` | 15 | W1 写入尝试上限。W1 write-attempt cap. | 增大提高机会但累积热量和破坏风险。More attempts add heat and corruption exposure. | 来自原 W1 循环常量。Copied from the original loop bound. |
| `stages.w1_settle_us` | 100000 | W1 每轮后稳定等待。W1 post-attempt settle. | 决定验证前是否给内核状态足够时间。Controls stabilization before verification. | 原实现使用 100 ms。Preserves 100 ms. |
| `stages.w1_scratch_repair_attempts` | 3 | W1 scratch 修复次数。W1 scratch-repair cap. | 更多修复可能恢复状态，也可能重复触碰损坏对象。More repairs may recover or compound damage. | 原实现限制三次。Matches the former three repairs. |
| `stages.w2_attempts` | 15 | W2 credential 写入尝试上限。W2 credential-write cap. | 影响提权成功机会、耗时和风险。Affects elevation probability, time, and risk. | 与旧 W2 循环一致。Matches legacy W2. |
| `stages.w2_settle_us` | 100000 | W2 每轮后等待。W2 settle delay. | 太短可能在 cred 尚未可见时验证。Too short may verify before visibility. | 保留旧 100 ms。Preserves 100 ms. |
| `stages.w3_chain_rounds` | 3 | W3 整链重复轮数。Whole W3-chain rounds. | 增大能覆盖偶发失配但延长高风险阶段。More rounds increase coverage and exposure. | 原流程最多三轮。Legacy cap is three. |
| `stages.w3_attempts` | 6 | 每轮 W3 尝试数。W3 attempts per round. | 影响 seccomp 绕过机会与线程压力。Changes bypass opportunity and contention. | 保留原六次上限。Preserves six attempts. |
| `stages.w3_settle_us` | 50000 | W3 尝试后等待。W3 settle delay. | 控制验证时序和速度。Controls verification timing. | 原实现使用 50 ms。Preserves 50 ms. |
| `routes.tcp_zerocopy.attempts` | 2000 | TCP 零拷贝路线爆破上限。TCP zerocopy probe cap. | 最大温升/耗时贡献之一；过低会降低命中率。Major heat/time contributor. | 等于原路线硬编码上限。Matches the original cap. |
| `routes.tcp_zerocopy.arm_sequence` | 16 | arm 阶段序列长度。Arm-sequence length. | 改变喷射/触发排列，错误值会错过窗口。Changes trigger ordering. | 保留已验证的 16 步序列。Preserves the validated 16-step sequence. |
| `routes.tcp_zerocopy.post_receive_hold_iterations` | 20000 | receive 后忙等保持次数。Post-receive hold iterations. | 增大保持窗口同时增加 CPU 热量。Longer hold widens window but adds heat. | 迁移原忙等常量，避免改变窗口。Copies the former busy-loop constant. |
| `routes.select_stack.enter_delay_us` | 50000 | consumer 进入后主触发延迟。Delay before main select trigger. | 直接平移 select 栈竞态窗口。Directly shifts the race window. | 已验证旧值为 50 ms。Preserves validated timing. |
| `routes.select_stack.timeout_us` | 200000 | 单次 select 超时。Per-select timeout. | 太短提前退出，太长拖慢失败恢复。Balances premature exit and recovery time. | 保持原 200 ms。Preserves 200 ms. |
| `routes.select_stack.consumer_max_calls` | 1 | consumer 最大调用数。Consumer call cap. | 多次调用改变原单次触发布局。Multiple calls alter the proven layout. | 当前稳定路径只调用一次。The stable path is single-shot. |
| `routes.select_stack.consumer_burst_calls` | 1 | 每批 consumer 调用数。Calls per consumer burst. | 增大批量会改变调度和栈存活时间。Changes scheduling and stack lifetime. | 维持单调用批次。Preserves one-call bursts. |
| `routes.multicast_waiter.ready_timeout_ms` | 10000 | multicast waiter 就绪超时。Waiter-ready timeout. | 太短误判慢调度，太长延迟清理。Balances slow scheduling and cleanup latency. | 原上限 10 秒。Matches legacy 10 seconds. |
| `routes.multicast_waiter.post_requeue_settle_us` | 200000 | requeue 后稳定等待。Post-requeue settle. | 影响 waiter 链稳定性；缩短可能过早改写。Affects waiter-chain stabilization. | 保留原 200 ms。Preserves 200 ms. |
| `routes.multicast_waiter.post_adjust_settle_us` | 100000 | 调整 waiter 后等待。Post-adjust settle. | 影响后续读写观察到的结构状态。Controls state visibility. | 保留原 100 ms。Preserves 100 ms. |
| `handoff.pre_dispatch_settle_ms` | 2000 | 向 KernelSU 交接前等待。Pre-handoff settle. | 太短可能在权限/SELinux 状态未稳定时启动。Too short may dispatch before privilege state settles. | 原流程等待 2 秒。Preserves two seconds. |
| `handoff.module_poll_attempts` | 30 | 模块加载状态轮询次数。Module-state poll count. | 与间隔共同决定 3 秒总窗口。Together forms the total window. | 30×100 ms 保持原 3 秒。Preserves three seconds. |
| `handoff.module_poll_interval_ms` | 100 | 模块轮询间隔。Module poll interval. | 更小增加唤醒，更大降低响应。Trades wakeups for latency. | 原粒度 100 ms。Legacy cadence. |
| `handoff.enforce_poll_attempts` | 200 | enforcing 状态轮询次数。Enforcing-state poll count. | 与间隔共同决定 20 秒恢复窗口。Defines the recovery window. | 200×100 ms 保留原 20 秒。Preserves twenty seconds. |
| `handoff.enforce_poll_interval_ms` | 100 | enforcing 轮询间隔。Enforcing poll interval. | 影响检测延迟与 CPU 唤醒。Affects detection latency and wakeups. | 原粒度 100 ms。Legacy cadence. |

任何改动都应记录设备、温度、核心组合、成功率和失败阶段。Every change must record device, temperature, CPU pair, success rate, and failing stage.
