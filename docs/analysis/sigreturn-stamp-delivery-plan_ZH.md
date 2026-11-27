# 计划：multicast_waiter 路由的 sigreturn stamp 投递

## 背景

multicast_waiter 路由用 `setsockopt(IPPROTO_IP, MCAST_BLOCK_SOURCE)` 把编码好的
waiter stamp 投递进受害线程的内核栈：内核经 IPv4 msfilter 路径把用户态缓冲区复制到
syscall 的栈帧上。在 waiter 足够深的内核上（5.15/6.1）这可行。

在浅帧 android12-5.10 世代（5.10.43/81/101）几何关闭。实测于黑鲨 KTUS-A0，
`5.10.101-android12-9-gece0cc04d1a5`：

- IPv4 msfilter 复制窗口最多携带 0x108 字节（栈深 864)，而要触及 waiter 的
  `task`/`lock` 词需要从窗口起点复制 480 字节；
- select_stack 无法替代：stale waiter 落在 pselect6 fd bitmap 的第 10 词，而 nfds
  被 `SELECT_STACK_ALLOC`(256 字节）锁死——第 15 词起落在内核清零的 `res_*`
  bitmap，因此 `task`/`lock`/`prio` 永远无法经 fd 位投递。

结果：在此世代两条原生路由都无法投递受控 waiter，而底层 race bug 完全存在且确定性。

## 方案

把同一编码 stamp 改走 `rt_sigreturn` 投递。内核的 `restore_fpsimd_context` 把信号帧的
512 字节 `vregs` 数组复制进 syscall 入口栈指针下方固定深度的内核栈缓冲区。在 5.10.101
上（反汇编验证的帧链 `rt_sigreturn(0x50) → restore_sigframe(0x60) →
restore_fpsimd_context(0x250)`）该缓冲区覆盖 `waiter_off = 320` 处的 stale waiter，
`task`/`lock` 在 stamp 偏移 368/376。与 msfilter 复制不同，此复制长度由 fpsimd record
布局固定，不受 socket 选项管道限制。

投递是一次性副作用：线程把 `sp` 指向构造帧（GPR 槽存活的寄存器值、`pc` 指向延续
标号）并调用 `rt_sigreturn`；恢复存活意味着复制落地，帧错误只会杀死本线程。

支撑改动：

1. **扁平 waiter 树编码**(`encode_multicast_waiter_trees`)：在 stamp 的 waiter 的
   `tree_entry` 与 `pi_tree_entry` 里种 `rb_parent_color = target - 8`、
   `rb_right = rb_left = 0`。叶节点的 `rb_erase` 随后确定性写 NULL 覆盖 `*(target)`。
2. **扁平 prio stamp**:5.10 的 `rt_mutex_waiter` 没有 `wake_state` 词，`prio` 在
   +0x40。在此 stamp `FAKE_WAITER_PRIO`，使 erase 门禁（`prio > 120`）通过。与上游
   PR #721 在 select 侧 stamp 的词相同。
3. **consumer 走队列节奏**：在 5.15/6.1 上对 waiter 的 `sched_setattr` 间歇失败，
   失败路径会走 PI futex 队列——这正是 reposition stale waiter 的机制。在此 5.10
   世代该调用从不失败，所以从不走队列。改为每次 consumer 调用都走队列；走的是
   5.15/6.1 本已频繁执行的同一代码路径，只是变为无条件。
4. **投递选择**：现有 `route.multicast_waiter` wire 节新增一个可选键
   `stamp_delivery`(`mcast` 默认 / `sigreturn`)。v2 对象节按 presence 表达：缺席
   的键保持逐字节旧行为。
5. **预算窗口 + enforce 自关窗**（仅 sigreturn 模式）:erase 在 consumer 的 PI
   walk 内异步落地。路由撑住窗口直到 consumer 耗尽 `consumer_max_calls` 预算或
   提前自关——对 `selinux_enforcing` 写入，consumer 每次调用后轮询
   `/sys/fs/selinux/enforce`，标志翻转即刻关窗。翻转后继续 consume 会重走队列并
   重复 erase 已出队的节点（开发中实测触发 panic)；而第一次调用后就拆窗则会与
   在途的 reposition 赛跑。其余内核永不进入此路径：`stamp_delivery` 缺席或
   `mcast` 时窗口仍在第一次 consumer 调用后关闭。

6. **fake waiter 页优先级词**：浅帧 5.10 `rt_mutex_waiter` 无 `wake_state` 字段，
   `prio` 在 +0x40。spray 页构建必须在 +0x40（5.10 flat）与 +0x44（6.1
   wake_state+prio 布局）同时写 `FAKE_WAITER_PRIO`；在 +0x40 写 0 会让该世代
   erase gate 的优先级比较读到 prio=0。
7. **exporter CPU 对**:`ProfileExporter` 此前硬编码 `CpuPairView(0, 1)`，覆盖一切
   profile 自著的 `selected_cpus`。现在尊重自著键值——大核热隔离机型（cpu5/6/7
   pin EINVAL 率实测 57%/28%/99.6%）需要把 main/consumer pin 进 little 簇。

前置依赖：pin-exact spray 修复（interleaved reclaim + 丢弃继承的
`/proc/<pid>/mem` pin)——缺了它们目标 slab 页在 spray 窗口内始终被 pin,
写入永不落地。

## 文件

- `src/core/route/sigreturn_stamp.{cpp,h}`——新增：帧构造器 + 击发 stub
- `src/core/route/multicast_waiter_route.cpp`——投递变体 + prio stamp
- `src/core/memory/payload_builder.{cpp,h}`——树编码 + stamp 读取器
- `src/core/race/threads.cpp`——consumer 走队列节奏
- `src/core/profile/binary.cpp`、`model.h` + Kotlin 路由配置——`stamp_delivery` 键
- `src/core/support/util.cpp`——fake waiter 页：prio 双写 +0x40/+0x44
- `profile-core/.../profile/ProfileExporter.kt`——尊重自著 selected_cpus
  （双侧，键名逐字一致）
- host 测试：stamp 构造器向量、树编码、键往返

## 行为差异

- `stamp_delivery` 缺席或为 `mcast`：无任何行为变化。sigreturn 帧构造器从不被调用，
  树/prio 种植从不执行，窗口照旧在首次 consumer 调用后关闭。
- consumer 走队列节奏：在 `sched_setattr` 会失败的内核上，走队列本就在失败时执行；
  变为无条件只是在成功路径上增加走队列次数。节奏仍受 `consumer_max_calls` 上界约束。
  这是唯一对 5.15/6.1 可见的行为变化，显式标注请重点审查。

## 验证

- `make -C src lsprivilege` 零告警；`native-host-tests` 20/20；`lint-tidy` 0 发现
- `tools/cmp_disasm.py` 对比改动前基线：攻击关键指令流在新/改函数之外保持一致
- 真机门禁：黑鲨 KTUS-A0（骁龙 8 Gen 1 / SM8450)，
  `5.10.101-android12-9-gece0cc04d1a5`，冷机、固定 CPU 对、multicast_waiter 路由
  `stamp_delivery=sigreturn`——全链、KernelSU 加载、SELinux 恢复 enforcing、
  exit 0、无 panic（证据按 device-gate 惯例归档；机制于 2026-11-27 在开发树验证）
