# 附：43499 侧的控制器与派发（含两处更正）

> 本件是 `seam-port-design.md` 的附件 · 2026-10-09 · 由 Lead 落盘（§13 与 §16 各更正了正文一处说法）

## 12 43499 侧的下一件：route 控制器（只读实测，待接线）

### 旧 `route/route_controller.h`（18 行）的形状

- `struct RouteController final { ghostlock::race::PiRace *race; const ghostlock::profile::TargetProfile *profile; ... }`
- 方法：`execute(const ghostlock::memory::WriteRequest *request) -> RouteStatus`

⇒ 也就是说：**控制器持有 race 状态与 profile** ⇒ 而 profile 这条依赖按 **ADR-0015** 在执行层**必须不存在** ✗ ⇒ 移植时它要变成：**race 状态由一个传入的结构给，值由脚本给** ✓（与 43284 侧 `SaMaterial` 同样的处理 ✓）。

### 旧 `route/multicast_waiter_route.cpp` 的结构（只读实测）

- 文件里有**两个** namespace 块：行 25–159 与行 165–207 ⇒ 形如「一份实现 + 一份另一形态（多半是 `#if` 分支或桩）」⇒ **移植前必须先分清哪一份是活的** ✗（我未逐行读，标为未量）。

### 因此 43499 侧接真控制器的顺序

1. 先分清那两份 namespace 块（一条命令：看两三个 `#if`/`#else` 行即可）；
2. 控制器按新形状写（race 状态传入、值由脚本给、**不持 profile** ✗）；
3. route 体按切片 1 表（22 处耦合）搬入；
4. 接上 `RouteExec` 缝 ⇒ 43499 侧**第一写**即可在 host 上闭环到真控制器 ✓。

**未量**：两个 namespace 块的分工；控制器对 race 状态的具体用法（未逐行读）。

## 13 更正（§12 的一处猜测被实测证伪）

§12 我写过：旧 multicast_waiter_route.cpp 的**两个 namespace 块**「多半是 #if 分支或桩」。**实测不成立**：

- 该文件顶部只有**三处** #if：`#if defined(__clang__)`（行 6，`#endif` 行 8）、`#ifndef _GNU_SOURCE`（行 10，`#endif` 行 12）⇒ 都**不在**两块 namespace 之间；
- 两块 `namespace … { }`（行 25–159 与 165–207）是**同一个实现的上下两段**（合法 C++），不是两条互斥分支。

⇒ 结论：**不存在「哪一份是活的」这个问题** ⇒ 移植时**两段都要读、都要搬**。

**教训（留一行）**：§12 那句是**我从形状猜的**，一条 `grep '^#if'` 就否掉了 ⇒ 本会话反复付价换来的规矩 —— **凡是能用一条命令量出来的，就不要用形状推断**。

## 14 43499 控制器与 route 派发：已读到行级（可据此直接落代码）

### 旧 route_controller.cpp（18 行全文已读）

```cpp
void RouteController::init(PiRace *race_ptr, const TargetProfile *profile_ptr) { race = …; profile = …; }
RouteStatus RouteController::execute(const WriteRequest *request) {
    if (!race || !profile || !request) return RouteStatus{.code = ROUTE_UNSUPPORTED};
    return run_route(*profile, request);            // 全部实质都在这里
}
```

⇒ 控制器本体只是**判空 + 转发** ⇒ 真正的活是 **`run_route(profile, request)`**。

### 旧 run_route 的位置

- `route_policy.hpp:224` `run_route_policy(const profile::TargetProfile &profile, …)`
- `route_policy.hpp:234` `run_route(...)`（派发）
- `route_policy.hpp:241` `status = run_route_policy<P>(profile, request);`

### 新树的现状（实测）

- **`run_route` 在新树里不存在**（全 `core/src/` 无命中）⇒ 要移植；
- 新 `route_policy.hpp` **存在**，且已经有编译期能力位 `RoutePolicyDefaults`：`multicast` / `w2_fast_repair` / `w3_exact_target` / `needs_two_words`（都是 `static constexpr bool`）⇒ 与旧树「policy 是 tag 类型、能力是类型上的常量」同构；
- 新 `run` 是**模板** `run<Policy, Profile>` ⇒ **它把 profile 做成了模板参数**。

### 由 ADR-0015 直接得出的改法（这是本节的要点）

新模板的 `Profile` 参数**就是**「执行层读 profile」这条路 ⇒ 按 ADR-0015 **是不允许的** ⇒ 移植时 `Profile` 必须换成**一组由脚本给的值**（与 43284 侧 `SaMaterial` 同一处理：结构按值传入 ✓ 执行层自己不取值 ✓）。

⇒ 于是 43499 侧接真控制器只剩三件、且都已行级明确：① `run_route` 的派发（旧 `route_policy.hpp:234`）+ 一条 route 体（切片 1 表 22 处耦合）；② `execute` 的判空（已读）；③ **`Profile` 模板参数 → 脚本给的值**。

## 15 旧 run_route 的派发体（已读，7 行）

```cpp
[[nodiscard]] inline RouteStatus run_route(const profile::TargetProfile &profile,
                                          const memory::WriteRequest *request) {
    RouteStatus status{.code = ROUTE_UNSUPPORTED};
    bool handled = false;
    for_each_policy([&]<class P>() {                       // 直接派发链，注释写明不用 std::visit
        if (!handled && P::supported(profile)) {
            status = run_route_policy<P>(profile, request);
            handled = true;
        }
    });
    return status;
}
```

⇒ 形状：**编译期策略链**（`for_each_policy` + 泛型 lambda 的 `<class P>`）✓，每条策略由 **`P::supported(profile)`** 把门 ✓，命中即 `run_route_policy<P>` ✓，未命中返回 `ROUTE_UNSUPPORTED` ✓。与旧注释一致：**不用 `std::visit`**（与窗口禁令同源 ✓）。

### 由此得到的移植清单（43499 侧，全部行级明确）

- `run_route`（本节）⇒ 搬到新树 ✓，但 **`profile` 换成脚本给的值**；
- `P::supported(profile)` ⇒ 换成 **`P::supported(values)`** ⇒ 而**新树的 `RoutePolicyDefaults` 已经有那四个能力位**（`multicast` / `w2_fast_repair` / `w3_exact_target` / `needs_two_words`）⇒ 映射是逐位对上的 ✓；
- `run_route_policy<P>`（旧 `:224`，其尾行是 `return Policy::run(request);`）⇒ 搬到新树，`profile` 同样换成值；
- `execute`（§14）⇒ 判空 + 转发 ⇒ 新树里对应 `RouteExec::execute(address, value, offset)`（我已落的缝 ✓）；
- route 体（切片 1 表，22 处耦合）⇒ 最后搬。

⇒ 结论：**43499 侧的派发层已无未知** ⇒ 剩下的未知只在 route 体内部（切片 1 表已逐行给出 ✓）。

## 16 我的「0 耦合」说法被实测收窄（aes-port 纠正）

我曾写「旧 `ipsec/aes256.cpp` 对旧会话与全局耦合 **0**」。该结论**口径太窄**：

- 我 grep 的模式是 `cve43499_state|g_exploit_session|g_direct_map_end|session::` ⇒ 只覆盖**会话/全局**这一类；
- 实测还有**一个直接文件依赖**：`aes256.cpp:9` include `ipsec/ipsec.hpp`，并在 `:219`/`:257`/`:258` 调它的 **`zeroize_bytes()`** 擦密钥表 ⇒ 而新树 `grep ipsec` **0 命中**（该件未搬）。

⇒ 准确说法是：**无会话/全局耦合，但有一个未搬的相邻件依赖**。

**处置（已准）**：在 `aes256.cpp` 匿名命名空间内放一个**同语义 file-local `zeroize_bytes`**（volatile 写 + null 检查，语义照 `ipsec.cpp:25-31`），**调用点逐行不变** ⇒ 两件自包含；并按本仓规矩**同批登记**：临时 · 收拢条件（`ipsec` 落树时改调它的）· 语义出处。

**教训**：`grep` 的**模式**决定结论的**口径** ⇒ 说「耦合 0」时必须写明**按什么模式量的**，否则那句话就是**过强**的。

> AES 那一件的判据验证与依赖更正（§17–§18）：见同目录 `seam-port-design-43499-aes.md`。

§19（43499 侧下一件的切法）已拆出：见同目录 `seam-port-design-43499-route.md`。
