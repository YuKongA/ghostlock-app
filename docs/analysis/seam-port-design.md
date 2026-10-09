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

## 7 第二个 backend（43284）的实测规模与它要用的通道

新树里 43284 基本是空的：只有 `lkm/module_facts.{hpp,cpp}`。旧树是一整个 backend：

- `cve_2026_43284/` 共 47 件，C++ 行数合计 10781 行
- 与访问路径相关的大件：`execution_binding.cpp`（350）、`diagnostic.cpp`（297）、`entry.cpp`（78）；`backend_terminal.cpp`（580）属终端，不是访问
- 通道面：`lkm/`（模块镜像与策略）、`ipsec/`（含 `aes256`）、以及页缓存相关件

它映射到访问接口的方式（与 43499 不同轴，但接口已能表达）：

- 43499 = 竞争窗口写，即 `AccessChannel::PiWindow` 加 `AccessCarrier::KernelPage`
- 43284 = 已加载模块或页缓存写，即 `AccessChannel::KernelModule` 或 `PageCache`，加 `AccessCarrier::ModuleWindow` 或 `FilePage`
- 这正是把通道与载体做成枚举而不是布尔的好处：两个 backend 用同一张接口，只有选择不同

因此 43284 的访问实现不是从零写，而是有选择地搬：搬它的写路径（模块面加它落地写的那一段），不搬终端与诊断（那是别的轴）。

还没量：这 10781 行里属于写路径的到底是多少（未按文件逐件分），以及它的 `initialize` 要准备什么（模块装载？镜像校验？）。动它的下一个切片前必须先量这两件。
