# 切片 2：窗口体（multicast）的映射表骨架

- 状态：**骨架**（两侧锚点已量到，逐行填充在下一轮）· 2026-10-09 · 上游 `seam-port-design.md` §11（三件裁定）
- 取材目标（裁定 ③）：**race 侧那次写**，不是 route 入口

## 1 旧侧（只读 `git show b0e9f499^:<path>`）

- 状态：`src/core/race/pi_race.h` 的 `PiRace` 字段 —— `consumer_success` / `consumer_inflight` / `route_delay_usec` / `fast_repair` / `main_cpu` / `consumer_cpu` / 三个 `PthreadOwner` / **`const memory::WriteRequest *request`** / `RouteStatus route_status`
- 线程体：`src/core/race/threads.cpp` 的 `waiter_thread`（`:32` 读 `race->request`）/ owner / consumer（三体各自读状态里的字段）
- route 体：`route/multicast_waiter_route.cpp`（208 行，22 处耦合；其中 `M:44` 是 `(void) request;` ⇒ **不吃 request**）

## 2 新侧（已量到的锚点）

- `race/pi_race.hpp`：`RaceOutcome`（`:35`）· **`FutexSeam`**（`:58`，静态直调，文件头写明遵守窗口规则 no indirect dispatch）· `run()` **在窗口守卫开启下被调用**（`:15`）
- `race/race_runtime.hpp`：**`RaceSettings`**（`:20`）· `SettingsVerdict`（`:36`）· **`check_settings`**（`:53`）· `AttemptOutcome`（`:64`）
- `race/pi_window.hpp`：`Prohibition`（`:36`）· `WindowViolations`（`:50`）· 守卫（`:66`）· **五条退出路径的回收计数断言**（`:23-25`）
- `race/futex_seam_linux.hpp`：平台侧 seam

## 3 映射骨架（每条待填旧 file:line）

- **写的那一次** ⇐ 旧 `threads.cpp` 的 waiter/owner 体里落 request 的那一小段 + `multicast_waiter_route.cpp` 的窗口内部分 ⇒ 新 `pi_race::run()` 的窗口体内 ⇒ **`Window::write_word(address, value, offset)` 的实现点**
- **address / value 的来源** ⇐ 旧 `race->request`（`pi_race.h` 的字段）⇒ 新形状里由 **`AccessParams`（route + offset）与 `KernelAccess::write` 的 address 实参**给（`contract/kernel_access.hpp`）⇒ 即 request 不再由 route 体读
- **时序两档**（`fast_repair`，裁定 ①）⇒ **不进窗口**；`RaceSettings` / `check_settings` 是它将来可能的归属，但**归属未定期间保持未移植**
- **暖场轮**（裁定 ②）⇒ 保留；要求 `MulticastLimits` 补 `arm_sequence` 字段，并把「0 = 代码内默认」改为**具名拒绝**（`pi_window.hpp:45` 禁止代码内调参数）
- **窗口内禁止项**（logging 等，切片 1 判 D 的 2 处）⇒ 读数移到窗口外，与 `WindowViolations` 的记账对齐

## 4 还没量（下一轮先做）

- 新 `pi_race::run()` 的**签名与它现在做到了哪一步**（当前只读到文件头与 seam 面，未读函数体）
- 旧 waiter/owner/consumer 三体里**哪一段真正落 request**（切片 1 表站在 route 侧，race 侧未逐段读）
- `MulticastLimits` 与 `RaceSettings` 是否该合并（前者 route 面，后者竞速面；暖场归属其中一侧）
