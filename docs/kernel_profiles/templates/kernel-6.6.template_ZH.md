# 6.6 内核 Profile 模板说明

模板文件：[kernel-6.6.template.conf](kernel-6.6.template.conf)。本文列出模板中的全部字段及其含义，可独立阅读。

> 模板里的必填项用 `null` 占位，在逐项填完之前故意保持无效，方便编辑器立即标出尚未完成的适配。不要为了消除告警把 `null` 随手改成 `0`。

<table>
<thead>
<tr><th>父项</th><th>子项</th><th>默认值</th><th>作用</th><th>影响</th><th>默认理由</th></tr>
</thead>
<tbody>
<tr><th rowspan="3">身份与路线选择</th><td><code>schema_version</code></td><td><code>1</code></td><td>配置结构版本，固定为 1；格式升级时用它判断兼容性。</td><td rowspan="3">错误值会改变目标选择、结构边界或竞态时序，可能导致失败、死锁、内存破坏、黑屏或重启。</td><td rowspan="3">该值保持 6.6 已验证基线，但不是稳定 ABI；新设备必须用同一目标镜像逐项复核。</td></tr>
<tr><td><code>release</code></td><td></td><td>与设备 <code>uname -r</code> 完全一致的版本串；文件名和匹配都用它。</td></tr>
<tr><td><code>kernel_major</code></td><td><code>6</code></td><td>内核主版本（5 或 6），供地址解析和合法性检查使用。</td></tr>
<tr><th rowspan="15">task_struct 结构偏移</th><td><code>task_struct.prio</code></td><td></td><td><code>task_struct.prio</code>，PI 优先级提升判定会读它。</td><td rowspan="15">错误值会改变目标选择、结构边界或竞态时序，可能导致失败、死锁、内存破坏、黑屏或重启。</td><td rowspan="15">该值保持 6.6 已验证基线，但不是稳定 ABI；新设备必须用同一目标镜像逐项复核。</td></tr>
<tr><td><code>task_struct.normal_prio</code></td><td></td><td>常规调度优先级字段。</td></tr>
<tr><td><code>task_struct.sched_task_group</code></td><td></td><td>调度组指针 <code>sched_task_group</code> 的偏移。</td></tr>
<tr><td><code>task_struct.pi_lock</code></td><td></td><td>PI 自旋锁 <code>pi_lock</code> 的偏移。</td></tr>
<tr><td><code>task_struct.pi_waiters</code></td><td></td><td>PI 等待队列链表偏移。</td></tr>
<tr><td><code>task_struct.pi_top_task</code></td><td></td><td>队列顶端任务指针偏移。</td></tr>
<tr><td><code>task_struct.pi_blocked_on</code></td><td></td><td>waiter 当前阻塞对象指针的偏移。</td></tr>
<tr><td><code>task_struct.pid</code></td><td></td><td>进程 PID 字段偏移。</td></tr>
<tr><td><code>task_struct.tgid</code></td><td></td><td>线程组 ID 字段偏移。</td></tr>
<tr><td><code>task_struct.atomic_flags</code></td><td></td><td><code>atomic_flags</code> 偏移，用于清理现场标记。</td></tr>
<tr><td><code>task_struct.real_cred</code></td><td></td><td><code>real_cred</code> 指针偏移。</td></tr>
<tr><td><code>task_struct.cred</code></td><td></td><td><code>cred</code> 指针偏移。</td></tr>
<tr><td><code>task_struct.comm</code></td><td></td><td><code>comm</code> 偏移；leaf 方向探测会读它。</td></tr>
<tr><td><code>task_struct.tasks</code></td><td></td><td>任务链表 <code>tasks</code> 偏移。</td></tr>
<tr><td><code>task_struct.seccomp</code></td><td></td><td><code>seccomp</code> 偏移；W3 清零其中的 <code>mode</code>。</td></tr>
<tr><th rowspan="5">credential 布局</th><td><code>cred.copy_size</code></td><td><code>136</code></td><td>伪造凭据时整体拷贝的字节数。</td><td rowspan="5">错误值会改变目标选择、结构边界或竞态时序，可能导致失败、死锁、内存破坏、黑屏或重启。</td><td rowspan="5">该值保持 6.6 已验证基线，但不是稳定 ABI；新设备必须用同一目标镜像逐项复核。</td></tr>
<tr><td><code>cred.usage_value</code></td><td><code>1</code></td><td><code>usage</code> 引用计数的目标值。</td></tr>
<tr><td><code>cred.caps_offset</code></td><td><code>48</code></td><td>capability 集合在 <code>cred</code> 中的偏移。</td></tr>
<tr><td><code>cred.caps_count</code></td><td><code>5</code></td><td><code>cred</code> 携带的 capability 数量。</td></tr>
<tr><td><code>cred.caps_value</code></td><td><code>-1</code></td><td>capability 位图的填充值。</td></tr>
<tr><th rowspan="2">waiter、堆与路线几何</th><td><code>kernelsnitch.collisions</code></td><td><code>4</code></td><td>KernelSnitch 需要的 futex 碰撞数量。</td><td rowspan="2">错误值会改变目标选择、结构边界或竞态时序，可能导致失败、死锁、内存破坏、黑屏或重启。</td><td rowspan="2">该值保持 6.6 已验证基线，但不是稳定 ABI；新设备必须用同一目标镜像逐项复核。</td></tr>
<tr><td><code>route.select_stack.waiter_shift</code></td><td></td><td>select 等待栈上 waiter 的相对位移（0 合法，负值常见）；作为回退时写在 <code>fallback.route.select_stack</code>。</td></tr>
<tr><th rowspan="9">内核相对符号</th><td><code>offset.init_task</code></td><td></td><td><code>init_task</code> 相对内核镜像基址的偏移。</td><td rowspan="9">错误值会改变目标选择、结构边界或竞态时序，可能导致失败、死锁、内存破坏、黑屏或重启。</td><td rowspan="9">该值保持 6.6 已验证基线，但不是稳定 ABI；新设备必须用同一目标镜像逐项复核。</td></tr>
<tr><td><code>offset.init_cred</code></td><td></td><td><code>init_cred</code> 偏移；W2 修复会写它的 +8。</td></tr>
<tr><td><code>offset.root_task_group</code></td><td></td><td><code>root_task_group</code> 偏移。</td></tr>
<tr><td><code>offset.selinux_enforcing</code></td><td></td><td><code>selinux_state.enforcing</code> 偏移；W1 写 0。</td></tr>
<tr><td><code>offset.selinux_blob_sizes</code></td><td></td><td><code>selinux_blob_sizes</code> 偏移。</td></tr>
<tr><td><code>offset.security_hook_heads</code></td><td></td><td><code>security_hook_heads</code> 偏移。</td></tr>
<tr><td><code>offset.slide_nfulnl_logger</code></td><td></td><td>KASLR 滑移定位锚点 <code>nfulnl_logger</code>。</td></tr>
<tr><td><code>offset.slide_boot_id</code></td><td></td><td>KASLR 滑移定位锚点（boot id 数据）。</td></tr>
<tr><td><code>offset.slide_loggers_0_1</code></td><td></td><td>KASLR 滑移定位锚点 <code>loggers[0..1]</code>。</td></tr>
<tr><th rowspan="2">推荐核心</th><td><code>execution.recommended_cpus.main</code></td><td><code>0</code></td><td>推荐主竞态线程核心。</td><td>改变调度、热量与竞态窗口；界面显式选择优先。</td><td>兼容旧的 0/1 回退且不假设设备拓扑。</td></tr>
<tr><td><code>execution.recommended_cpus.consumer</code></td><td><code>1</code></td><td>推荐消费者核心。</td><td>与主核心同核会增加互相抢占；不存在的核心会失败或回退。</td><td>与主核心分离并保持旧默认。</td></tr>
<tr><th rowspan="3">堆准备</th><td><code>execution.heap.prepare_max_attempts</code></td><td><code>4</code></td><td>堆准备的完整重试上限。</td><td>增大可提高偶发恢复率，也增加耗时和温升。</td><td>原实现最多四轮，迁移时保持行为。</td></tr>
<tr><td><code>execution.heap.prepare_timeout_ms</code></td><td><code>240000</code></td><td>单次堆准备总超时。</td><td>太短会误判慢设备；太长会延迟失败反馈。</td><td>4 分钟覆盖旧最坏路径且仍有上界。</td></tr>
<tr><td><code>execution.heap.kernelsnitch_timeout_ms</code></td><td><code>60000</code></td><td>KernelSnitch 扫描等待上限。</td><td>缩短可能漏掉结果，增大会延长卡住状态。</td><td>原扫描窗口为 60 秒。</td></tr>
<tr><th rowspan="3">共享竞态时序</th><td><code>execution.race.route_wait_ms</code></td><td><code>1000</code></td><td>路线启动前共享等待。</td><td>影响线程/对象就绪顺序。</td><td>1 秒是旧实现的保守稳定窗口。</td></tr>
<tr><td><code>execution.race.setup_settle_us</code></td><td><code>50000</code></td><td>竞态准备后的稳定等待。</td><td>太短会在资源未就绪时触发。</td><td>原 50 毫秒平衡可靠性和速度。</td></tr>
<tr><td><code>execution.race.state_poll_interval_us</code></td><td><code>1000</code></td><td>共享状态轮询间隔。</td><td>越小处理器占用越高，越大响应越迟。</td><td>1 毫秒保持旧轮询粒度。</td></tr>
<tr><th rowspan="8">W1/W2/W3 阶段</th><td><code>execution.stages.w1_attempts</code></td><td><code>15</code></td><td>W1 写入尝试上限。</td><td>增大提高机会但累积热量和破坏风险。</td><td>来自原 W1 循环常量。</td></tr>
<tr><td><code>execution.stages.w1_settle_us</code></td><td><code>100000</code></td><td>W1 每轮后稳定等待。</td><td>决定验证前是否给内核状态足够时间。</td><td>原实现使用 100 毫秒。</td></tr>
<tr><td><code>execution.stages.w1_scratch_repair_attempts</code></td><td><code>3</code></td><td>W1 临时区修复次数。</td><td>更多修复可能恢复状态，也可能重复触碰损坏对象。</td><td>原实现限制三次。</td></tr>
<tr><td><code>execution.stages.w2_attempts</code></td><td><code>15</code></td><td>W2 凭证写入尝试上限。</td><td>影响提权成功机会、耗时和风险。</td><td>与旧 W2 循环一致。</td></tr>
<tr><td><code>execution.stages.w2_settle_us</code></td><td><code>100000</code></td><td>W2 每轮后等待。</td><td>太短可能在凭证尚未可见时验证。</td><td>保留旧 100 毫秒。</td></tr>
<tr><td><code>execution.stages.w3_chain_rounds</code></td><td><code>3</code></td><td>W3 整链重复轮数。</td><td>增大能覆盖偶发失配但延长高风险阶段。</td><td>原流程最多三轮。</td></tr>
<tr><td><code>execution.stages.w3_attempts</code></td><td><code>6</code></td><td>每轮 W3 尝试数。</td><td>影响 seccomp 绕过机会与线程压力。</td><td>保留原六次上限。</td></tr>
<tr><td><code>execution.stages.w3_settle_us</code></td><td><code>50000</code></td><td>W3 尝试后等待。</td><td>控制验证时序和速度。</td><td>原实现使用 50 毫秒。</td></tr>
<tr><th rowspan="3">TCP 零拷贝路线</th><td><code>execution.routes.tcp_zerocopy.attempts</code></td><td><code>2000</code></td><td>TCP 零拷贝路线爆破上限。</td><td>最大温升/耗时贡献之一；过低会降低命中率。</td><td>等于原路线硬编码上限。</td></tr>
<tr><td><code>execution.routes.tcp_zerocopy.arm_sequence</code></td><td><code>16</code></td><td>准备阶段序列长度。</td><td>改变喷射/触发排列，错误值会错过窗口。</td><td>保留已验证的 16 步序列。</td></tr>
<tr><td><code>execution.routes.tcp_zerocopy.post_receive_hold_iterations</code></td><td><code>20000</code></td><td>接收后忙等保持次数。</td><td>增大保持窗口同时增加处理器热量。</td><td>迁移原忙等常量，避免改变窗口。</td></tr>
<tr><th rowspan="4">Select 栈路线</th><td><code>execution.routes.select_stack.enter_delay_us</code></td><td><code>50000</code></td><td>消费者进入后、主 select 触发前延迟。</td><td>直接平移 select 栈竞态窗口。</td><td>已验证旧值为 50 毫秒。</td></tr>
<tr><td><code>execution.routes.select_stack.timeout_us</code></td><td><code>200000</code></td><td>单次 select 超时。</td><td>太短提前退出，太长拖慢失败恢复。</td><td>保持原 200 毫秒。</td></tr>
<tr><td><code>execution.routes.select_stack.consumer_max_calls</code></td><td><code>1</code></td><td>消费者最大调用数。</td><td>多次调用改变原单次触发布局。</td><td>当前稳定路径只调用一次。</td></tr>
<tr><td><code>execution.routes.select_stack.consumer_burst_calls</code></td><td><code>1</code></td><td>每批消费者调用数。</td><td>增大批量会改变调度和栈存活时间。</td><td>维持单调用批次。</td></tr>
<tr><th rowspan="3">多播等待者路线</th><td><code>execution.routes.multicast_waiter.ready_timeout_ms</code></td><td><code>10000</code></td><td>多播等待者就绪超时。</td><td>太短误判慢调度，太长延迟清理。</td><td>原上限 10 秒。</td></tr>
<tr><td><code>execution.routes.multicast_waiter.post_requeue_settle_us</code></td><td><code>200000</code></td><td>重新入队后稳定等待。</td><td>影响等待者链稳定性；缩短可能过早改写。</td><td>保留原 200 毫秒。</td></tr>
<tr><td><code>execution.routes.multicast_waiter.post_adjust_settle_us</code></td><td><code>100000</code></td><td>调整等待者后等待。</td><td>影响后续读写观察到的结构状态。</td><td>保留原 100 毫秒。</td></tr>
<tr><th rowspan="5">提权交接</th><td><code>execution.handoff.pre_dispatch_settle_ms</code></td><td><code>2000</code></td><td>向 KernelSU 交接前等待。</td><td>太短可能在权限/SELinux 状态未稳定时启动。</td><td>原流程等待 2 秒。</td></tr>
<tr><td><code>execution.handoff.module_poll_attempts</code></td><td><code>30</code></td><td>模块加载状态轮询次数。</td><td>与间隔共同决定 3 秒总窗口。</td><td>30×100 毫秒保持原 3 秒。</td></tr>
<tr><td><code>execution.handoff.module_poll_interval_ms</code></td><td><code>100</code></td><td>模块轮询间隔。</td><td>更小增加唤醒，更大降低响应。</td><td>原粒度 100 毫秒。</td></tr>
<tr><td><code>execution.handoff.enforce_poll_attempts</code></td><td><code>200</code></td><td>强制状态轮询次数。</td><td>与间隔共同决定 20 秒恢复窗口。</td><td>200×100 毫秒保留原 20 秒。</td></tr>
<tr><td><code>execution.handoff.enforce_poll_interval_ms</code></td><td><code>100</code></td><td>强制状态轮询间隔。</td><td>影响检测延迟与处理器唤醒。</td><td>原粒度 100 毫秒。</td></tr>
</tbody>
</table>
