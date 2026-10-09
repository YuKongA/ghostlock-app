# 设计：把 43499 的缝接到一条 route 上（实测路径）

- 状态：**草案**（动工前必过评审）· 2026-10-09 · 基线 `23f9d746`
- 上游：`contract/kernel_access.hpp`（接口）· `backend/cve_2026_43499/kernel_access.hpp`（实现，决策与缝分离）

## 1 缝要填的四件事（已定）

- `bring_up(route, target)` —— 起 route，备好它要的东西；**会话级**（initialize → shutdown）
- `tear_down()` —— 拆 route，归还 bring_up 备的；同上
- `land_word(address, value, offset)` —— 经窗口落**一个字**；**一次 access**（竞争作用域）
- `can_read()` / `read_word(...)` —— 默认 **false**（窗口只写）

## 2 实测映射（旧树 `b0e9f499^` 逐条量过）

- `route/route_api.hpp`：`do_pselect_fake_lock_route(const memory::WriteRequest *) -> RouteStatus`、`do_tcp_fake_lock_route(...)`、`do_kernel5_fake_lock_route(...)` ⇒ **就是 `land_word`**（它本来就收 `WriteRequest`）
- `bootstrap.{hpp,cpp}`：`resolve_profile_addresses()`、`install_profile()`，文件头自称 from the backend run_setup ⇒ **就是 `bring_up`**（去掉 profile 取值，按 ADR-0015 那些值由脚本传入）
- `primitives.hpp`：`Cve43499Primitives::write_word<M>(...)`、`zero_word<M>(...)`（模板，机制 M）⇒ **`land_word` 的内层**（M = 选中那条 route）
- `route/route_lifecycle.hpp`：一个**概念**（route 类的结构契约）⇒ **形状参照**（新的缝是它的极简版）

**关键有利事实**：新树**已经有** `memory/write_request.hpp`（`WriteRequest::make`、`WriteMode`、`PayloadWriteLayout`，且已有测试）⇒ 所以 `land_word` 是「构造 `WriteRequest` → 调 `do_*_route`」 ⇒ **是接线，不是重写**。

## 3 必须先切断的依赖（真正的工作量）

- 旧 route 与 primitives 依赖 **旧全局会话 `session::CoreSession`**，新树里**没有** ⇒ 移植时必须把「从哪取目标事实」换成**传入的 `TargetState`**（`mm_struct`/`page_base`/`fake_task`/`fake_lock`，正是 `session.hpp` 里那四个）
- 旧树两个全局（会话单例与 direct map 上限）⇒ **不得随移植带回来**（新代码不得引入可变全局）
- 旧 `RouteStatus` ⇒ 映到新 `KernelAccess::Status`，映射表落在**缝里**，不散进 route 代码

## 4 首批范围（先打通一条 route）

1. 新件 `backend/cve_2026_43499/seam/multicast_seam.{hpp,cpp}`（**只做一条**）
2. `bring_up` = 旧 bootstrap 的两步（去 profile 取值）
3. `land_word` = 构造 `WriteRequest` → `do_multicast_fake_lock_route`（从旧树**按行**搬，不带全局）
4. host 测：一个**假窗口**（不真写）验「参数 → WriteRequest → route 调用」的字段一致性
5. **真机**：这就是目标里的**临时方法** —— 用生产路径上的一次最小调用把 `KernelAccess` 跑起来，取证后**同批登记删除计划**（仅服务实验的入口必须自带清理计划）

## 5 未定（不猜）

- `fd_set` 前戏（旧 `fdset_put_word`、`reserve_standard_io`）属 route 内部还是 `bring_up` ⇒ 搬时按行核
- 机制 `M` 在选中 route 后如何静态绑定 ⇒ 倾向：每条 route 一个缝子类，`bring_up` 时按 `RouteId` **直接 switch**，不用虚分派
- `RouteStatus` 的全部取值 ⇒ 需逐值映射（未逐个读）

## 6 实测体量（2026-10-09，动工前量）

- `route/multicast_waiter_route.cpp` = **208 行**，其中对旧会话与两个全局的引用 **22 处**
- `primitives.cpp` = **202 行**
- `route/route_middleware.cpp` / `route_controller.cpp` 各 **18 行**
- ⇒ 一条 route 的完整搬移约 **400 行**，且是**攻击关键路径**代码

**因此切片（下一轮按此做，不越片）**：

1. **切片 1**：22 处耦合的**替换表**（逐处写：旧写法 ⇒ 新写法，取自 `TargetState` 或 `AccessParams`）——**先出表再动代码**
2. **切片 2**：窗口体（`Window::write_word` 的实现，208 行里属于 Window 的那部分）
3. **切片 3**：`primitives` 的内层（若切片 2 需要）

**验收（每片都要满足）**：host 构建 0 告警 · `make -C core check` 绿 · 不引入任何可变全局 · 缝的测试保持绿（它钉的是决策，不应因窗口变真而改）

> 第二个 backend（43284）的实测规模、写面、上下文与耦合数：见同目录 `seam-port-design-43284.md`。

## 11 三件待裁：Lead 的裁定（据证据，不等指令）

### ① `race.fast_repair` 的运行期 latch —— 不进窗口体

证据：它服务的是 **steps 层的 W2 快速修复**（旧 `steps.cpp:89,99`），旧读者是 **waiter 线程**（`threads.cpp:44`：fast 则固定 20 ms 超时，否则 `race_route_wait_ms()`）⇒ 它是**竞速时序的两档选择**，不是窗口内的写。

裁定：**不搬进窗口**，也**不凭旧默认值带回**（ADR-0015：这类值由脚本给）。⇒ 落地前它的**新归属**要定：要么成为脚本可设的**时序档**，要么明确**不保留**并记录；在此期间**保持未移植**（不静默等价）。

### ② 暖场轮：**保留**

证据：旧 `M:119` 有 `attempt < arm_sequence` 的暖场轮（只发 setsockopt、**不 arm** consumer），而新件 `run_multicast_handshake`（`multicast_policy.hpp:85-98`）**每轮都 arm** ⇒ 行为**不等价**。

裁定：**保留暖场**。理由是本仓既有的判据顺序 —— **旧行为是真机验证过的**，新件尚未；在真机证明之前，**不得**以更简单为理由改变竞速时序。⇒ 切片 2 必须把暖场轮做回（`MulticastLimits` 缺 `arm_sequence` 字段 ⇒ 同批补字段，并把「0 = 用代码内默认」改为**具名拒绝**，因为 `pi_window.hpp:45` 禁止代码内调参数）。

### ③ 写怎么落到那次写：**改的是调用对象，不是缝的形状**

队友的实测纠正了我 §4.3 的说法：multicast 的 route 体**不吃 `WriteRequest`**（`M:44` 实测 `(void) request;`），request 由 **race 侧线程**读（`pi_race.h:87`、`threads.cpp:33`）。

裁定：**缝的形状不变**（`Window::write_word(address, value, offset)` 仍然对），**变的只是谁实现它** —— 实现者要落到 **race 侧那次写**（它持有 address/value 与 fake lock 的原语），而**不是**调 `do_multicast_fake_lock_route`。⇒ 我 §4.3 的那句就此作废（本节即其更正），切片 2 的取材目标是 **race 侧的写路径**。
