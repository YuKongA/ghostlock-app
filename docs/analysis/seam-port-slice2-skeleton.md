# 切片 2：窗口体（multicast）的映射表骨架

- 状态：**骨架**（两侧锚点已量到，逐行填充在下一轮）· 2026-10-09 · 上游 `landing-port-design.md` §11（三件裁定）
- 取材目标（裁定 ③）：**race 侧那次写**，不是 route 入口

## 1 旧侧（只读 `git show b0e9f499^:<path>`）

- 状态：`src/core/race/pi_race.h` 的 `PiRace` 字段 —— `consumer_success` / `consumer_inflight` / `route_delay_usec` / `fast_repair` / `main_cpu` / `consumer_cpu` / 三个 `PthreadOwner` / **`const memory::WriteRequest *request`** / `RouteStatus route_status`
- 线程体：`src/core/race/threads.cpp` 的 `waiter_thread`（`:32` 读 `race->request`）/ owner / consumer（三体各自读状态里的字段）
- route 体：`route/multicast_waiter_route.cpp`（208 行，22 处耦合；其中 `M:44` 是 `(void) request;` ⇒ **不吃 request**）

## 2 新侧（已量到的锚点）

- `race/pi_race.hpp`：`RaceOutcome`（`:35`）· **`FutexOps`**（`:58`，静态直调，文件头写明遵守窗口规则 no indirect dispatch）· `run()` **在窗口守卫开启下被调用**（`:15`）
- `race/race_runtime.hpp`：**`RaceSettings`**（`:20`）· `SettingsVerdict`（`:36`）· **`check_settings`**（`:53`）· `AttemptOutcome`（`:64`）
- `race/pi_window.hpp`：`Prohibition`（`:36`）· `WindowViolations`（`:50`）· 守卫（`:66`）· **五条退出路径的回收计数断言**（`:23-25`）
- `race/futex_ops_linux.hpp`：平台侧 landing

## 3 映射骨架（每条待填旧 file:line）

- **写的那一次** ⇐ 旧 `threads.cpp` 的 waiter/owner 体里落 request 的那一小段 + `multicast_waiter_route.cpp` 的窗口内部分 ⇒ 新 `pi_race::run()` 的窗口体内 ⇒ **`Window::write_word(address, value, offset)` 的实现点**
- **address / value 的来源** ⇐ 旧 `race->request`（`pi_race.h` 的字段）⇒ 新形状里由 **`AccessParams`（route + offset）与 `KernelAccess::write` 的 address 实参**给（`contract/kernel_access.hpp`）⇒ 即 request 不再由 route 体读
- **时序两档**（`fast_repair`，裁定 ①）⇒ **不进窗口**；`RaceSettings` / `check_settings` 是它将来可能的归属，但**归属未定期间保持未移植**
- **暖场轮**（裁定 ②）⇒ 保留；要求 `MulticastLimits` 补 `arm_sequence` 字段，并把「0 = 代码内默认」改为**具名拒绝**（`pi_window.hpp:45` 禁止代码内调参数）
- **窗口内禁止项**（logging 等，切片 1 判 D 的 2 处）⇒ 读数移到窗口外，与 `WindowViolations` 的记账对齐

## 4 还没量（下一轮先做）

- 新 `pi_race::run()` 的**签名与它现在做到了哪一步**（当前只读到文件头与 landing 面，未读函数体）
- 旧 waiter/owner/consumer 三体里**哪一段真正落 request**（切片 1 表站在 route 侧，race 侧未逐段读）
- `MulticastLimits` 与 `RaceSettings` 是否该合并（前者 route 面，后者竞速面；暖场归属其中一侧）

## 5 三件未量项之一：已量 —— 落点就是 pi_race::run

新 `race/pi_race.hpp`（149 行）实测：

- `prepare(std::uint32_t *chain_word, std::uint32_t *target_word)` ⇒ `Prepared` / `NotPrepared`
- **`run(WindowGuard &window)`** ⇒ **签名自带守卫** ⇒ 它**就是在窗口内跑的那一次** ⇒ 返回落在 `RaceOutcome` 轴上（未 prepare 则 `Refused`）
- `RaceOutcome`：`NotPrepared` / `Prepared` / `Running` / `Disarmed` / `Destroyed` / `Refused`
- 文件头自述：已实现「一次尝试的形状（两个 PI futex 字、worker 角色、结果）」；futex 操作以**编译期 policy** 提供（no vtable / no function pointer）

⇒ 结论：`Window::write_word(address, value, offset)` 的实现点就是（或就在）**`pi_race::run` 的窗口体内** ⇒ 切片 2 不必猜接在哪，锚点已定。

## 6 一处与新裁定冲突的表述（本轮新发现）

同一文件的「未实现」段写道：几何、时序与重试预算 **come from the profile（Q2 rule 1）**。

⇒ 这与 **ADR-0015** 冲突：该裁定把值改为**经 Lua 传入**（执行层不直接读 profile）。⇒ 处置：该段表述**随切片 2 一并改写**（改为由脚本传入），并且**不得**在实现里留任何从 profile 取值的路径（否则就是同一份数据的**第二条读取路径**，正是本仓明令禁止的高危形态）。**未改动该文件**，先在表里登记。

## 7 剩下两件：已量

### ① 谁真正落写：waiter 线程，经 route 控制器执行 request

实测 `src/core/race/threads.cpp` 三个入口：`waiter_thread`（`:31`）· `owner_thread`（`:85`）· `consumer_thread`（`:108`）。真正落写的一行是 **`:65` 的 `race->route_status = controller.execute(request);`** —— 它在 `waiter_thread` 体内（31 与 85 之间）。`:216` 再把 `route_status` 读回。

⇒ ③ 的更正可以写到**行级**：窗口的实现点 = **`controller.execute(request)`** 这一次调用（request 由 race 侧构造持有），**不是** route 体（route 体拿到 request 但 `M:44` 实测忽略它）。⇒ 缝的 `land_word` 要做的是「用 address/value/offset **构造 request**，再调 `controller.execute`」—— 即我 §4.3 的原句**一半是对的**（要构造 request），错在**谁消费它**（不是 route 体，是控制器）。

### ② `MulticastLimits` 与 `RaceSettings`：不合并

- `MulticastLimits`（route 面）：`max_attempts` · `arm_hold`
- `RaceSettings`（竞速面）：`main_cpu` · `consumer_cpu` · `settle_us` · `poll_us` · `poll_rounds`（末者是**派生量**，文件头写明不是独立设置）

字段**不相交**，且归属**不同层**（route 策略 vs 竞速运行时）⇒ 裁定：**不合并**。理由是单一权威 —— 合并会让**两层共写一个结构**，那与「同一份事实两条路径」同类，同样被禁。

⇒ 推论：**暖场轮（裁定 ②）的 `arm_sequence` 归 `MulticastLimits`**（它是 route 侧的 arm 策略，不是竞速时序）。
