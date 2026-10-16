# 43499 设备层接口扩展设计（L 级 · 待认可）

> 阶段：J7.5。目标语境：「成功分别用两个 backend 获取 root；如果做不到则说明接口能力不足，**改接口**而不是往 native 加逻辑。」
> 本文只描述**接口形态**与分工，不含实现；未获认可前不写码（AGENTS §2 的 L 级要求）。

## 1. 事实（都有出处）

- 现役 43499 段：`initialize code=0` ✓、`snapshot code=0` ✓，而 `prepare` / `writeWord` / `finishVictim` = `op-not-wired:*` ✗。
- 退役真身（`git show 6f7ded87:src/core/backend/cve_2026_43499/primitives.cpp`）：
  - `syscall(__NR_perf_event_open, …)`（76）、`mmap(… MAP_SHARED, fd, 0)`（85）、`ioctl(PERF_EVENT_IOC_ENABLE/DISABLE)`（93-95）+ 500000 次 `getpid` 的泵（94）。
  - `attack_write<M>(…)`：**一次写 = 堆喷 + PI race 事务**（142-171）；三条策略 `SelectPolicy` / `TcpPolicy` / `MulticastPolicy`。
- 我方现存：route 类在（`core/src/backend/cve_2026_43499/landing/select_stack_route.hpp` 等），**设备层原语不在** ✗。

## 2. 结论（接口能力不足）

现有 device-ops 只能传少量标量（每 op 一两个入参）⇒ **无法表达**这台引擎所需的输入：

1. 一组 perf 事件（每个的 `type`/`config`/`flags`/数量，以及环形缓冲大小与页数）；
2. 泵的时序参数（ioctl 序列与次数）；
3. 堆喷目标与 bucket 布局（大小、个数、memfd 规格）；
4. route 选择与每策略的独立参数（如 `consumer_max_calls`/`consumer_burst_calls`；`arm_sequence`/`arm_hold`）。

## 3. 接口扩展（建议形态，全部是**值**）

```text
Prepare43499        : perf[MAX] { type, config, flags } · ring { pages, wakeup } · spray { size, count } · pump { iters }
WriteWord43499       : route_id · word { addr, value } · retries（上限由 Kotlin 给）
FinishVictim43499    : victim { pid/task 由前一步读出 } · cleanup { flags }
```

契约单表随批扩（`core/src/jni/attack_contract_fields.def` ⇒ `./gradlew exportJniAttackContract` 生成 DTO），
Kotlin 侧装配进 `Assembled43499`（与 `Assembled43284` 同形），策略与顺序**只在 Kotlin**。

## 4. 分工边界（红线）

- native 侧：只做**设备动作**（`perf_event_open`/`mmap`/`ioctl`/`socketpair`/`memfd`/`write` 等 syscall 级调用）与数据搬运。
- Kotlin 侧：route 选择、泵的节奏、喷多少、重试上限、成功判据、收尾顺序（**全部判断**）。
- 不把退役 `primitives.cpp` 的策略/竞态逻辑译进 C++（那正是要避免的「C++ 高级逻辑」）。

## 5. 验收（未过不进下一步）

1. `./gradlew checkAll` = 132/132（只增不减）；Android 0 警告；`verifyJniLibs` ✓。
2. 真机（冷机、固定 CPU 对、单 route、KernelSU 未加载）：`prepare/writeWord/finishVictim` 由 `op-not-wired` 变为**具名推进**；
   任一拒绝都带名字与 errno。
3. 归档 `docs/analysis/device-gates/`（单份 ≤8 KB）。

## 6. 与「真实 profile」的关系

第 2 条里的四个输入**大部分来自 GLK profile**（`NativeProfile` 已能读出 `heap.prepare_max_attempts`、
`routes.select_stack.consumer_burst_calls`、`routes.multicast_waiter.arm_sequence/arm_hold` 等 ✓）⇒
本设计同时就是「真实 profile 进一步设计」的落点：profile 提供值，Kotlin 装配，native 只执行。

## 7. 未决（待你裁定）

- `perf[MAX]` 的上限取多少（退役按固定容量；我方需要定一个可审计的常数）。
- 泵的次数（500000）是否进 profile（我倾向：进 ✓，作为可审计值）。
- 三条 route 是否同批全接，还是先 Select 一条走通（我倾向：**先 Select 一条** ✓，与「单 route」门禁一致）。
