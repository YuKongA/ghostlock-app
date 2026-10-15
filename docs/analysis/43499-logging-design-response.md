# design-critic 审查的逐条回应（43499 日志点设计）· 2026-10-12

> 审查记录：design-critic v0.2 · 冻结 HEAD `6a6a2404` · 被审件 sha256 前 16 `2e5c0c1344617858`
> 总判 **不通过** · 阻断 **1**（L1）· 重要 **7**（L2–L8）· 建议 **4**（L9–L12）

## M6 处置（复审 v0.2 ⇒ 本文 §L2 的错述更正 ✗）

> **口径更新（2026-10-12）**：本节的 **M6 旧口径**（「`multicast_window.hpp` 不属窗口体、不纳入账本」）**已被取代** ✗ —— 该文件已按指令**纳入 `WINDOW-DIRTY` 账本**（4 批），**Lead 本轮裁定保留** ✓；以「**inside PI**」口径与账本现状为准。

```
**错述** ✗：本文 §L2 曾写扫描面已扩到**三个**文件（含 `landing/multicast_window.hpp`）✗
  ⇒ critic 实测：该文件**既不在** `Makefile:1118` 的 sed 列表 ✗ **也不是** guard 的目标 ✗
**更正与裁定** ✓：**该文件不属于窗口体** ⇒ **不纳入**扫描面 ✓，理由按**窗口体定义**（本稿 §L1 ✓）：
  · 窗口体 ＝ `multicast_route.hpp` 的 `trigger`／`waiter_main`／`owner_main`／`judge`／`consumer_main` ✓
  · `multicast_route_linux.hpp`（futex 包装 ✓）在 `Makefile:1118` 的 sed 列表里 ✓
  · ⇒ 二者合起来**正好覆盖窗口体** ✓；⇒ `multicast_window.hpp` **不在**定义内 ✗ ⇒
    **⇒ 纳入它只会造成"覆盖比定义宽"的假安全** ✗（⇒ 检查一个不运行在窗口内的文件，既无收益又让清单失真 ✓）
**⇒ M6 关闭**：这是**更正**而非补覆盖 ✓（⇒ 实现与文档现已一致 ✓）。
```
>
> **后续**：L1／L7／L9 已在复审 v0.2 判为**闭合**（L1 条件 M8 已满足 ✓）；⇒ **M6 的处置见文末** ✓
> 依 AGENTS §7：设计者**逐条回应**；**阻断未关不得进批准 gate** ✓

## L1（阻断）—— **接受** ✓，并且它指出了一处**我造成的既有违规** ✗✓

```
critic 的要点（逐字 ✓）：「race/pi_window.hpp 的禁令含 no logging/stdio inside the window body
  (it allocates and may block)」⇒ 而我的 §2 C6 要求**窗口时序内**每轮/每分支都有点 ⇒
  ⇒ 照设计实现即违规 ✗；不实现则 C1/C3 破 ✗ ⇒ 两条路都不通 ✓。且「窗口体」**无定义** ⇒ 无法判定 ✓。
**⇒ 我接受 ①（窗口内禁日志）** ✓，并补一条设计（critic 未指出、但同源 ✓）：
   ⇒ **⇒ 本会话我加的 8 个点里，落在窗口内的那 6 个（`waiter` / `owner` / `trigger` / `requeue` 的入口与分支）
      **本身就违反该禁令** ✗✓（⇒ 当时我以 printf 为榜样的"打点"其实越了线 ✗）。
**处置**：这 6 个点改为 critic ① 的"**窗口外汇总**"形态（进入一条 ＋ 退出一条 ＋ 命中/失败结果一条 ✓）
  ⇒ 且**窗口体内只留预分配环形缓冲**（POD ✓ 零分配 ✓ 零阻塞 ✓）⇒ 出窗口统一 flush ＋ **打印丢弃计数** ✓。
```

**并补 L1 要求的「窗口体」可判定定义** ✓：

```
**PI 内（定义 · 口径更新）** ＝ 只约束 **inside PI** 的那几段，**不是整个窗口体函数** ✗（口径：**PI 内无 syscall · 无分配 · 无打印**；**PI 之后可打印**）。
   · **边界（锚点为权威）**：`waiter_main` **`PI-IN`** → **`PI-OUT`**；`owner_main` **`PI-IN`** → **`PI-OUT`**；`trigger` **`PI-IN-TRIGGER`** → **`PI-OUT-TRIGGER`**（其后 PI 未结束）；`consumer_main` **`PI-IN-CONSUMER`** → **`PI-OUT-CONSUMER`**；`judge` 在 PI 之外（豁免 **`PI-EXEMPT-JUDGE`**）。
   · **行号会漂移** ⇒ 权威 = 代码 `PI-*` 注释标记；原文见 [annex-anchors](pi-window-constraints-annex-anchors.md)。
   · 时间区间：**一个 attempt 内** ✓（⇒ 不是整轮 ✓ 不是整进程 ✓）
   · **PI 内允许**：`shared_` 字段的写入 ✓（⇒ 记 `(tag, tsc, a, b)` ✓ 不格式化 ✗）；
     **PI 内禁止**：`spdlog`（⇒ 异步也会在调用线程做 sink_it_ ✗，且默认 `block` 溢出策略会**阻塞调用线程** ✗✓）、
     任何 `std::string`/`printf`/`new` ✓；**PI 内也不得写环**（环已撤除，改由 `shared_` 字段 + **PI 之后**打印承载）
   · 出 PI 的**唯一** flush 点：**PI 结束之后**（`judge()` 返回之后同样适用 ✓；由调用方在 PI 外展开 ✓）
```

## L2（重要）—— **接受** ✓

```
"WINDOW-DIRTY 检查只 sed `pi_window.hpp` 与 `pi_race.hpp`" ✗ ⇒ 覆盖不到**实际运行窗口的 TU**（`landing/multicast_route.hpp` ✗）。
**处置**：把扫描面扩到窗口实际运行的文件 ✓（`landing/multicast_route.hpp` ✓／`landing/multicast_route_linux.hpp` ✓／
  `landing/multicast_window.hpp` ✓），并同步扩禁用词（⇒ 加 `spdlog` ✓／`log_step` ✓）；
  ⇒ 判据：**在 `multicast_route.hpp` 里临时插一个 `spdlog` 调用 ⇒ 检查必须 FAIL** ✓（§8 证伪 ✓）。
```

## L3 / L4 / L5 / L7 / L11（重要/建议）—— **接受** ✓（⇒ 合入设计下一版 ✓）

```
L3：C1 由"任何 if/else"收敛为**可判定集合** ✓：① 唯一码表里每个**具名拒绝**对应一行 ✓；
    ② 攻击链路上给出**函数清单（file:line）**，逐函数的**入口**与**每个 outcome 分支**各一条 ✓；
    ③ 判据＝由 §4 的 grep/测试执行 ✓（⇒ 删一个点即红 ✓）。
L4：增「点 → 数字键名」表 ✓（逐点列键名 ✓）＋ §4 给**逐键 grep -c** 判据 ✓。
L5：增「printf 分类与迁移」一栏 ✓：① 诊断类 ⇒ 迁日志（给清单 ✓）；② **产物/协议类 ⇒ 保留 printf** ✓
    （⇒ 如 `tools/profile_manifest_dump.cpp` ✗ ⇒ 排除在 C3 之外并**写明理由** ✓）；
    ③ 判据＝迁移后计数下降 ✓（⇒ 实测基线 **316 处** ✓ 由 critic 提供 ✓）。
L7：增 `log_shutdown()`（`flush()` ＋ `spdlog::shutdown()`）✓，且 D4 的退出码行放在 drain **之后** ✓；
    判据：真机日志**末尾必有退出码行** ✓ 且 = **进程真实退出码**（`rc=9` ⇒ `value=9`；旧「Lua 0/10」**已取代**）⇒ 去 drain ⇒ 缺失/非末行 ⇒ 红 ✓。
L11：验收改**覆盖式** ✓（逐键 grep -c ≥1 ✓ ＋ 删除即红的证伪 ✓）；「≥2000 行」降为**参考指标** ✗。
```

## L6 / L8 / L9 / L10 / L12 —— **接受** ✓

```
L6：API 面补全 ✓：`log_step_values`（变参 ✓ 5–8 值 ✓）／`log_step_str(level, what, key, text)` ✓（⇒ C5 具名串 ✓）／
    `log_hex(level, what, u64, width)` ✓（⇒ 定宽度/大小写 ✓）；键名格式 `k=v` ✓（⇒ 支撑 L4 的 grep ✓）。
L8：新具名串**二选一**✓：优先**复用既有码表** ✓（`plan_gate.hpp:14/:54` ✓／`script_surface.hpp:357` ✓）；
    确需新码 ⇒ **同批登记**并在 §2 标注「本批登记」✓；C5 判据＝每个码可在码表找到逐字相同项 ✓。
L9：`log_start` 里显式 `set_pattern`（含 `%Y-%m-%d %H:%M:%S.%e` ✓ · `%t` 线程标识 ✓ · `%l` ✓），
    并把「pattern 已 pin」作为 C4 的**可失败**判据 ✓（⇒ 去掉 `%t` ⇒ 红 ✓）。
L10：增三项 ✓：日志**体积上限/轮转** ✓（⇒ 长跑不写爆设备 ✓）＋ **默认级别** ✓ ＋ **丢弃计数行** ✓（⇒ L1① 的环形缓冲配套 ✓）。
L12：覆盖补三处 ✓：① **门面每层**（`MulticastLanding` ✓／`MulticastWindow` ✓／`RouteExec` ✓／`Access` ✓）进入 ＋ 具名拒绝 ✓；
    ② **fork 子进程侧**一条 ✓；③ **三步各自**完整链（进入 → 写槽 → 验证 → 结果 ✓，⇒ 窗口内不便者按 L1① 用汇总替代 ✓）。
```

## 处置顺序（按 §7：阻断先关 ✓）

```
① **先关 L1**：把 8 点里的 6 个窗口内点改为「窗口外汇总」✓ ＋ 落「窗口体定义/允许禁止清单」✓
   ＋ 环形缓冲（POD ✓ 零分配 ✓）＋ 丢弃计数 ✓
② 再关 L2（WINDOW-DIRTY 扫描面 ＋ 证伪）✓
③ 然后按 L3–L12 修订设计文档（⇒ 与 ① ①② 同批落 ✓）
④ 复审：把修订稿交回 design-critic ⇒ 阻断归零后才进实现 ✓
```
