# 43499 准备步骤（`prepare`）— 设计计划（2026-10-10）

**为什么单独一件**：`w1/w2/w3` 的执行体已就位（阶段节奏 + 一次尝试 + 窗口守卫 ⇒ `window_runner`），唯一缺口是**两个字从哪来**。这一步把「leak 搜索 → arm → prepare 页 → 两个字」接起来，接上后 `step-words-unprepared` 消失。级别 L（跨十余个单元、只在设备上可验）⇒ 先设计后改动。

## 一、目标与判据

- 目标：给定 wire 里 `backend.cve_2026_43499.execution.heap.*` 的调参，产出准备状态：（a）`chain` 字、（b）`target` 字、（c）已武装的 multicast 载体句柄；三者交给 `WindowStageContext`。
- 判据：设备上 `w1` 首次尝试即 `RaceOutcome::Running`（内核搬了一个等待者），或给出具名失败而**不是**继续跑。
- 非目标：本步不做写页、不做交结；它只把窗口摆好。

## 二、单元与顺序（全部已有，纯函数为主）

1. 设备事实：`abi` / `kmi_catalog` / `device_facts`（内核 release、KASLR 基址、结构偏移）
2. 布局：`layout_from_document` + `memory/write_layout` + `memory/shaping_plan`（页几何、预算）
3. 发现：`leak/snitch_policy` → `leak/snitch_run` → `leak/snitch_scan` → `leak/snitch_discovery`（配合 `leak/futex_hash`、`collision_*`、`candidate_match`）
4. 区域与边界：`leak/snitch_region` → `leak/scan_geometry` → `leak/scan_bounds` → `leak/leak_walk`
5. 回收与整形：`memory/reclaim_plan` → `memory/prepare_plan` → `memory/prepare_page`
6. 武装：`memory/multicast_arm`（`check_geometry` → `encode_multicast_arm`）+ `seam/multicast_route_linux`（真正把 datagram 交出去）
7. 产出：两个 futex 字（`chain`/`target`）与守卫；交给 `window_runner`。

参考实现（**不是**可照搬的成品，是串法的参照）：`core/src/pipeline/run_pipeline_temp.cpp`（1120 行，含 plan/gate 与门拼装）。

## 十六、2026-10-11 深夜：43499 准备阶段**整条跑通**（status=ok）

- 盲区＝`patience.poll_rounds`：fixture 里**没有这个键** ⇒ main 读成 0 ⇒ LeakLoop 循环 0 次 ⇒
  「`found=false` 而 `address≠0`」的矛盾由此完全解释（`address()` 来自调用方预启动的那次搜索 ✓）。
- 补上 `poll_rounds=4`（与 `main_cpu=0`）后，设备上：

```
main: unit loop saw=1 asked=1 poll_rounds=4 address=0xffff8000000da000
prepare status=ok step=11        ← 准备成功（开/等/归还全过，账目此前已平）
refused (step-words-unprepared)  ← 只剩「把两个字交给窗口步骤」这一步交接
```

- 同一批缺失的键已补进所有 43499 fixture（`poll_rounds` / `main_cpu`）——与早前 `futex_table_size` 那次同类：
  **键补进 schema 与测试，却忘了补进文档值**。这条已可当自查清单项 ✓。
- 下一步（纯接线）：把准备的产物（**同一个桶里的那两个字** + 对齐后的 base）交给窗口步骤，
  `step-words-unprepared` 就该消失。

## 十七、2026-10-11 凌晨：w1 真的跑起来了（step-words-unprepared 消失）

- 交接完成：窗口上下文 = 准备时按桶摆好的**两个字** + 调用方持有的 `WindowGuard`（≤64 B 值对象，无需"武装"步骤产出它）+ 对齐后的 base；阶段数值取 wire 的 `stages.*`（每步一支，字段同名）。
- 设备结果：

```
prepare status=ok step=11        ← 准备成功
refused (step-window-not-won)    ← w1 真的尝试了，阶段自己的判决：没赢下窗口
```

⇒ 43499 链条现在是：wire → 值 → 三段映射 → 区域排布 → 发现（命中 ✓）→ 准备（status=ok ✓）→ **窗口阶段真跑**（阶段级判决 ✓）。剩下的 `step-window-not-won` 属**行为面**（竞态要赢下窗口），不再是接线面。

- 结构收获：字与守卫的归属都在**调用方**这一层（`WordsRegion` + `WindowGuard`）——准备的映射随它返回即失效，这点已写进头文件。

## 三十、win 不了的**结构性**原因：本树只有 route 的策略层，没有 controller

- `core/src/backend/cve_2026_43499/route/` 现有：`route_policy` / `multicast_policy` / `policy_dispatch` /
  `lifecycle` / `expected_keys` / `route_status` —— **全是策略与判据**；
- 退役里"赢下窗口"的真正判据不是 `cmp_requeue_pi` 的计数，而是**等待者被改挂后跑的那半段 route**：
  `RouteController controller; controller.init(race, &profile); controller.execute(request);` ⇒
  本树**没有 `RouteController`**（也没有那半段的执行体）⇒ 这才是 w1 停在 `step-window-not-won` 的结构性原因。
- 与 §二十九 的签名互证：`val3` 相符、持有者相符、等待者确实在等 ⇒ 改挂本身按内核要求已尽力；
  真正缺的是**改挂之后要跑的东西**（multicast 写路径 + 生命周期对象）。
- 判断：这是**独立的一批**（route 执行体移植），不是"再修一处参数"。按规矩（L 级、先计划后写码、单独批次）
  需要一份批次计划与你的点头；在生产路径上继续试参数已无意义（§二十九 已穷尽）。
