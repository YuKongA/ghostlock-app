# 6.12 内核 Profile 模板说明

模板 JSON：[kernel-6.12.template.json](kernel-6.12.template.json)。本文包含全部字段，不依赖共有字段说明。

> 模板在所有必填空值（`"字段": ,`）填写完成前会刻意保持为非法 JSON，使 IDE 立即标出尚未完成的设备适配。不要为了通过语法检查而把空值随意改成 `0`。

<table>
<thead>
<tr><th>父项</th><th>子项</th><th>默认值</th><th>作用</th><th>影响</th><th>默认理由</th></tr>
</thead>
<tbody>
<tr><th rowspan="3">身份与路线选择</th><td><code>schema_version</code></td><td><code>1</code></td><td>定义 6.12 攻击路线消费的 <code>schema_version</code> 配置值。</td><td rowspan="3">错误值会改变目标选择、结构边界或竞态时序，可能导致失败、死锁、内存破坏、黑屏或重启。</td><td rowspan="3">该值保持 6.12 已验证基线，但不是稳定 ABI；新设备必须用同一目标镜像逐项复核。</td></tr>
<tr><td><code>release</code></td><td></td><td>定义 6.12 攻击路线消费的 <code>release</code> 配置值。</td></tr>
<tr><td><code>kernel_major</code></td><td><code>6</code></td><td>定义 6.12 攻击路线消费的 <code>kernel_major</code> 配置值。</td></tr>
<tr><th rowspan="15">task_struct 结构偏移</th><td><code>task_prio</code></td><td></td><td>定义 6.12 攻击路线消费的 <code>task_prio</code> 配置值。</td><td rowspan="15">错误值会改变目标选择、结构边界或竞态时序，可能导致失败、死锁、内存破坏、黑屏或重启。</td><td rowspan="15">该值保持 6.12 已验证基线，但不是稳定 ABI；新设备必须用同一目标镜像逐项复核。</td></tr>
<tr><td><code>task_normal_prio</code></td><td></td><td>定义 6.12 攻击路线消费的 <code>task_normal_prio</code> 配置值。</td></tr>
<tr><td><code>task_sched_task_group</code></td><td></td><td>定义 6.12 攻击路线消费的 <code>task_sched_task_group</code> 配置值。</td></tr>
<tr><td><code>task_pi_lock</code></td><td></td><td>定义 6.12 攻击路线消费的 <code>task_pi_lock</code> 配置值。</td></tr>
<tr><td><code>task_pi_waiters</code></td><td></td><td>定义 6.12 攻击路线消费的 <code>task_pi_waiters</code> 配置值。</td></tr>
<tr><td><code>task_pi_top_task</code></td><td></td><td>定义 6.12 攻击路线消费的 <code>task_pi_top_task</code> 配置值。</td></tr>
<tr><td><code>task_pi_blocked_on</code></td><td></td><td>定义 6.12 攻击路线消费的 <code>task_pi_blocked_on</code> 配置值。</td></tr>
<tr><td><code>task_pid</code></td><td></td><td>定义 6.12 攻击路线消费的 <code>task_pid</code> 配置值。</td></tr>
<tr><td><code>task_tgid</code></td><td></td><td>定义 6.12 攻击路线消费的 <code>task_tgid</code> 配置值。</td></tr>
<tr><td><code>task_atomic_flags</code></td><td></td><td>定义 6.12 攻击路线消费的 <code>task_atomic_flags</code> 配置值。</td></tr>
<tr><td><code>task_real_cred</code></td><td></td><td>定义 6.12 攻击路线消费的 <code>task_real_cred</code> 配置值。</td></tr>
<tr><td><code>task_cred</code></td><td></td><td>定义 6.12 攻击路线消费的 <code>task_cred</code> 配置值。</td></tr>
<tr><td><code>task_comm</code></td><td></td><td>定义 6.12 攻击路线消费的 <code>task_comm</code> 配置值。</td></tr>
<tr><td><code>task_tasks</code></td><td></td><td>定义 6.12 攻击路线消费的 <code>task_tasks</code> 配置值。</td></tr>
<tr><td><code>task_seccomp</code></td><td></td><td>定义 6.12 攻击路线消费的 <code>task_seccomp</code> 配置值。</td></tr>
<tr><th rowspan="5">credential 布局</th><td><code>cred_copy_size</code></td><td><code>136</code></td><td>定义 6.12 攻击路线消费的 <code>cred_copy_size</code> 配置值。</td><td rowspan="5">错误值会改变目标选择、结构边界或竞态时序，可能导致失败、死锁、内存破坏、黑屏或重启。</td><td rowspan="5">该值保持 6.12 已验证基线，但不是稳定 ABI；新设备必须用同一目标镜像逐项复核。</td></tr>
<tr><td><code>cred_usage_value</code></td><td><code>1</code></td><td>定义 6.12 攻击路线消费的 <code>cred_usage_value</code> 配置值。</td></tr>
<tr><td><code>cred_caps_offset</code></td><td><code>48</code></td><td>定义 6.12 攻击路线消费的 <code>cred_caps_offset</code> 配置值。</td></tr>
<tr><td><code>cred_caps_count</code></td><td><code>5</code></td><td>定义 6.12 攻击路线消费的 <code>cred_caps_count</code> 配置值。</td></tr>
<tr><td><code>cred_caps_value</code></td><td><code>-1</code></td><td>定义 6.12 攻击路线消费的 <code>cred_caps_value</code> 配置值。</td></tr>
<tr><th rowspan="2">waiter、堆与路线几何</th><td><code>kernelsnitch_collisions</code></td><td><code>4</code></td><td>定义 6.12 攻击路线消费的 <code>kernelsnitch_collisions</code> 配置值。</td><td rowspan="2">错误值会改变目标选择、结构边界或竞态时序，可能导致失败、死锁、内存破坏、黑屏或重启。</td><td rowspan="2">该值保持 6.12 已验证基线，但不是稳定 ABI；新设备必须用同一目标镜像逐项复核。</td></tr>
<tr><td><code>pselect_waiter_shift</code></td><td></td><td>定义 6.12 攻击路线消费的 <code>pselect_waiter_shift</code> 配置值。</td></tr>
<tr><th rowspan="9">内核相对符号</th><td><code>off_init_task</code></td><td></td><td>定义 6.12 攻击路线消费的 <code>off_init_task</code> 配置值。</td><td rowspan="9">错误值会改变目标选择、结构边界或竞态时序，可能导致失败、死锁、内存破坏、黑屏或重启。</td><td rowspan="9">该值保持 6.12 已验证基线，但不是稳定 ABI；新设备必须用同一目标镜像逐项复核。</td></tr>
<tr><td><code>off_init_cred</code></td><td></td><td>定义 6.12 攻击路线消费的 <code>off_init_cred</code> 配置值。</td></tr>
<tr><td><code>off_root_task_group</code></td><td></td><td>定义 6.12 攻击路线消费的 <code>off_root_task_group</code> 配置值。</td></tr>
<tr><td><code>off_selinux_enforcing</code></td><td></td><td>定义 6.12 攻击路线消费的 <code>off_selinux_enforcing</code> 配置值。</td></tr>
<tr><td><code>off_selinux_blob_sizes</code></td><td></td><td>定义 6.12 攻击路线消费的 <code>off_selinux_blob_sizes</code> 配置值。</td></tr>
<tr><td><code>off_security_hook_heads</code></td><td></td><td>定义 6.12 攻击路线消费的 <code>off_security_hook_heads</code> 配置值。</td></tr>
<tr><td><code>off_slide_nfulnl_logger</code></td><td></td><td>定义 6.12 攻击路线消费的 <code>off_slide_nfulnl_logger</code> 配置值。</td></tr>
<tr><td><code>off_slide_boot_id</code></td><td></td><td>定义 6.12 攻击路线消费的 <code>off_slide_boot_id</code> 配置值。</td></tr>
<tr><td><code>off_slide_loggers_0_1</code></td><td></td><td>定义 6.12 攻击路线消费的 <code>off_slide_loggers_0_1</code> 配置值。</td></tr>
<tr><th rowspan="2">推荐核心</th><td><code>recommended_cpus.main</code></td><td><code>0</code></td><td>推荐主竞态线程核心。</td><td>改变调度、热量与竞态窗口；界面显式选择优先。</td><td>兼容旧的 0/1 回退且不假设设备拓扑。</td></tr>
<tr><td><code>recommended_cpus.consumer</code></td><td><code>1</code></td><td>推荐消费者核心。</td><td>与主核心同核会增加互相抢占；不存在的核心会失败或回退。</td><td>与主核心分离并保持旧默认。</td></tr>
<tr><th rowspan="3">堆准备</th><td><code>heap.prepare_max_attempts</code></td><td><code>4</code></td><td>堆准备的完整重试上限。</td><td>增大可提高偶发恢复率，也增加耗时和温升。</td><td>原实现最多四轮，迁移时保持行为。</td></tr>
<tr><td><code>heap.prepare_timeout_ms</code></td><td><code>240000</code></td><td>单次堆准备总超时。</td><td>太短会误判慢设备；太长会延迟失败反馈。</td><td>4 分钟覆盖旧最坏路径且仍有上界。</td></tr>
<tr><td><code>heap.kernelsnitch_timeout_ms</code></td><td><code>60000</code></td><td>KernelSnitch 扫描等待上限。</td><td>缩短可能漏掉结果，增大会延长卡住状态。</td><td>原扫描窗口为 60 秒。</td></tr>
<tr><th rowspan="3">共享竞态时序</th><td><code>race.route_wait_ms</code></td><td><code>1000</code></td><td>路线启动前共享等待。</td><td>影响线程/对象就绪顺序。</td><td>1 秒是旧实现的保守稳定窗口。</td></tr>
<tr><td><code>race.setup_settle_us</code></td><td><code>50000</code></td><td>竞态准备后的稳定等待。</td><td>太短会在资源未就绪时触发。</td><td>原 50 毫秒平衡可靠性和速度。</td></tr>
<tr><td><code>race.state_poll_interval_us</code></td><td><code>1000</code></td><td>共享状态轮询间隔。</td><td>越小处理器占用越高，越大响应越迟。</td><td>1 毫秒保持旧轮询粒度。</td></tr>
<tr><th rowspan="8">W1/W2/W3 阶段</th><td><code>stages.w1_attempts</code></td><td><code>15</code></td><td>W1 写入尝试上限。</td><td>增大提高机会但累积热量和破坏风险。</td><td>来自原 W1 循环常量。</td></tr>
<tr><td><code>stages.w1_settle_us</code></td><td><code>100000</code></td><td>W1 每轮后稳定等待。</td><td>决定验证前是否给内核状态足够时间。</td><td>原实现使用 100 毫秒。</td></tr>
<tr><td><code>stages.w1_scratch_repair_attempts</code></td><td><code>3</code></td><td>W1 临时区修复次数。</td><td>更多修复可能恢复状态，也可能重复触碰损坏对象。</td><td>原实现限制三次。</td></tr>
<tr><td><code>stages.w2_attempts</code></td><td><code>15</code></td><td>W2 凭证写入尝试上限。</td><td>影响提权成功机会、耗时和风险。</td><td>与旧 W2 循环一致。</td></tr>
<tr><td><code>stages.w2_settle_us</code></td><td><code>100000</code></td><td>W2 每轮后等待。</td><td>太短可能在凭证尚未可见时验证。</td><td>保留旧 100 毫秒。</td></tr>
<tr><td><code>stages.w3_chain_rounds</code></td><td><code>3</code></td><td>W3 整链重复轮数。</td><td>增大能覆盖偶发失配但延长高风险阶段。</td><td>原流程最多三轮。</td></tr>
<tr><td><code>stages.w3_attempts</code></td><td><code>6</code></td><td>每轮 W3 尝试数。</td><td>影响 seccomp 绕过机会与线程压力。</td><td>保留原六次上限。</td></tr>
<tr><td><code>stages.w3_settle_us</code></td><td><code>50000</code></td><td>W3 尝试后等待。</td><td>控制验证时序和速度。</td><td>原实现使用 50 毫秒。</td></tr>
<tr><th rowspan="3">TCP 零拷贝路线</th><td><code>routes.tcp_zerocopy.attempts</code></td><td><code>2000</code></td><td>TCP 零拷贝路线爆破上限。</td><td>最大温升/耗时贡献之一；过低会降低命中率。</td><td>等于原路线硬编码上限。</td></tr>
<tr><td><code>routes.tcp_zerocopy.arm_sequence</code></td><td><code>16</code></td><td>准备阶段序列长度。</td><td>改变喷射/触发排列，错误值会错过窗口。</td><td>保留已验证的 16 步序列。</td></tr>
<tr><td><code>routes.tcp_zerocopy.post_receive_hold_iterations</code></td><td><code>20000</code></td><td>接收后忙等保持次数。</td><td>增大保持窗口同时增加处理器热量。</td><td>迁移原忙等常量，避免改变窗口。</td></tr>
<tr><th rowspan="4">Select 栈路线</th><td><code>routes.select_stack.enter_delay_us</code></td><td><code>50000</code></td><td>消费者进入后、主 select 触发前延迟。</td><td>直接平移 select 栈竞态窗口。</td><td>已验证旧值为 50 毫秒。</td></tr>
<tr><td><code>routes.select_stack.timeout_us</code></td><td><code>200000</code></td><td>单次 select 超时。</td><td>太短提前退出，太长拖慢失败恢复。</td><td>保持原 200 毫秒。</td></tr>
<tr><td><code>routes.select_stack.consumer_max_calls</code></td><td><code>1</code></td><td>消费者最大调用数。</td><td>多次调用改变原单次触发布局。</td><td>当前稳定路径只调用一次。</td></tr>
<tr><td><code>routes.select_stack.consumer_burst_calls</code></td><td><code>1</code></td><td>每批消费者调用数。</td><td>增大批量会改变调度和栈存活时间。</td><td>维持单调用批次。</td></tr>
<tr><th rowspan="3">多播等待者路线</th><td><code>routes.multicast_waiter.ready_timeout_ms</code></td><td><code>10000</code></td><td>多播等待者就绪超时。</td><td>太短误判慢调度，太长延迟清理。</td><td>原上限 10 秒。</td></tr>
<tr><td><code>routes.multicast_waiter.post_requeue_settle_us</code></td><td><code>200000</code></td><td>重新入队后稳定等待。</td><td>影响等待者链稳定性；缩短可能过早改写。</td><td>保留原 200 毫秒。</td></tr>
<tr><td><code>routes.multicast_waiter.post_adjust_settle_us</code></td><td><code>100000</code></td><td>调整等待者后等待。</td><td>影响后续读写观察到的结构状态。</td><td>保留原 100 毫秒。</td></tr>
<tr><th rowspan="5">提权交接</th><td><code>handoff.pre_dispatch_settle_ms</code></td><td><code>2000</code></td><td>向 KernelSU 交接前等待。</td><td>太短可能在权限/SELinux 状态未稳定时启动。</td><td>原流程等待 2 秒。</td></tr>
<tr><td><code>handoff.module_poll_attempts</code></td><td><code>30</code></td><td>模块加载状态轮询次数。</td><td>与间隔共同决定 3 秒总窗口。</td><td>30×100 毫秒保持原 3 秒。</td></tr>
<tr><td><code>handoff.module_poll_interval_ms</code></td><td><code>100</code></td><td>模块轮询间隔。</td><td>更小增加唤醒，更大降低响应。</td><td>原粒度 100 毫秒。</td></tr>
<tr><td><code>handoff.enforce_poll_attempts</code></td><td><code>200</code></td><td>强制状态轮询次数。</td><td>与间隔共同决定 20 秒恢复窗口。</td><td>200×100 毫秒保留原 20 秒。</td></tr>
<tr><td><code>handoff.enforce_poll_interval_ms</code></td><td><code>100</code></td><td>强制状态轮询间隔。</td><td>影响检测延迟与处理器唤醒。</td><td>原粒度 100 毫秒。</td></tr>
</tbody>
</table>
