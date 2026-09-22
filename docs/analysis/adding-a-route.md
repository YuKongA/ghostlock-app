# 新增一条攻击链（route）开发指南

route 配置已可插拔。新增一条 route 需改的地方已收敛到下面这些。以假想 route `foo_waiter`
（wire = 4）为例。

> 约定：新 wire 值必须同时登记在 native 与 Kotlin 的 route 目录；两侧一致性测试会因漂移失败。
> **Legacy profile 已定型，新增 route 不涉及 legacy**（不改 `legacy/offsets_json.cpp` /
> `LegacyProfileConverter.kt`）。当前 route 配置走 GLK1 二进制。

---

## 总览

Native（4 处）：

| 改什么 | 文件 | 内容 |
|---|---|---|
| 目录项 | `profile/model.h` | `RouteKind` 加一项（+ `kRouteCatalog` 一行，可选） |
| Policy | `route/route_policy.hpp` | 加 `FooPolicy` 并追加到 `RoutePolicyList`（1 行） |
| Route 文件 | `route/foo_route.{h,cpp}` | Route 类 + driver + **Procedure 子类 + `make_foo_procedure`** |
| 工厂/声明 | `route/route_api.hpp`、`route/exploit_procedure.cpp` | 声明 `make_foo_procedure`；工厂加 1 个 case |

Kotlin（1 处）：`data/Profile.kt` `RouteKind` 加一项。

> 说明：`RoutePolicyVariant`、`run_route`、fallback 查找、capability 分派都由
> `RoutePolicyList`/`for_each_policy` 自动派生，无需手工维护。
> Policy 必须 host-safe（会被 `route_controller.cpp` 主机编译）；Procedure 依赖
> `common.h`/session，只能 production 编译——因此 **Procedure 折进 route 文件并置于
> `#if defined(__ANDROID__)` 内**，不再单独成文件。

---

## 1. Kotlin 侧

### 1.1 `data/Profile.kt` — 加 `RouteKind` 项
```kotlin
internal enum class RouteKind(val wire: Int, val token: String) {
    TCP_ZEROCOPY(1, "tcp_zerocopy"),
    SELECT_STACK(2, "select_stack"),
    MULTICAST_WAITER(3, "multicast_waiter"),
    FOO_WAITER(4, "foo_waiter"),        // 新增
    ;
    companion object { fun fromToken(...); fun fromWire(...) }
}
```
`NativeProfile.routeKind()` 已由 `RouteKind.fromToken` 派生，无需改。

### 1.2 其余
- `data/NativeProfile.kt`：新增 `FooConfig : RouteConfig` 子类型承载 route 专属参数，接 `routeEntries()`/`applyRouteEntry()`/`emptyRouteConfig()`/`from()`；**不要加进 `NativeProfileDocument`/`ExecutionTuning`**。
- `ui/FieldLabels.kt`：route 展示标签（可选）。
- `app/src/test/.../RouteCatalogAgreementTest.kt`：把 `(token, wire)` 加入 canonical 列表。
- `app/src/main/assets/kernel_profiles/*.conf`：route 块示例（`route { foo_waiter { ... } }`，必要时 `to = "<fallback>"`）。

---

## 2. Native 侧

### 2.1 `profile/model.h` — 目录项
```cpp
enum class RouteKind : uint8_t { Auto=0, TcpZerocopy=1, SelectStack=2, MulticastWaiter=3, FooWaiter=4 };
inline constexpr uint8_t kRouteFooWaiter = static_cast<uint8_t>(RouteKind::FooWaiter);

inline constexpr RouteCatalogEntry kRouteCatalog[] = {
    {"tcp_zerocopy", kRouteTcpZerocopy},
    {"select_stack", kRouteSelectStack},
    {"multicast_waiter", kRouteMulticastWaiter},
    {"foo_waiter", kRouteFooWaiter},        // 可选（仅一致性测试/legacy 需要）
};
```

### 2.2 `route/route_policy.hpp` — 新增 Policy + 追加注册
```cpp
struct FooPolicy {
    static constexpr RouteKind kind = RouteKind::FooWaiter;
    static constexpr bool multicast = false;          // 收尾是否需 ghost disarm
    static constexpr bool w2_fast_repair = false;     // W2 预热/激活修复
    static constexpr bool w3_exact_target = false;    // W3 是否跳过 leaf 探测
    static constexpr bool tcp_payload_layout = false; // prepare_skb_payload 的 tcp 几何
    static constexpr bool allows_fallback = false;    // 是否允许 profile 声明的 fallback

    static bool supported(const profile::TargetProfile &p) noexcept { return p.supports(kind); }
    static RouteStatus run(const memory::WriteRequest *request) {
        return do_foo_fake_lock_route(request);
    }
};

using RoutePolicyList = std::tuple<SelectPolicy, TcpPolicy, MulticastPolicy, FooPolicy>;  // 追加
```
`RoutePolicyVariant` 等由上面这一行自动派生。

### 2.3 `route/foo_route.{h,cpp}` — Route 类 + driver + Procedure（一个文件搞定）
头文件：声明 route 类（满足 `RouteLifecycle`，见 `route/route_lifecycle.hpp`）。
```cpp
class FooRoute final {
public:
    FooRoute(ghostlock::race::PiRace *race, const ghostlock::memory::WriteRequest *request,
             const ghostlock::profile::TargetProfile &profile) noexcept;
    [[nodiscard]] int32_t prepare() noexcept;      // 0 = 就绪
    [[nodiscard]] ghostlock::route::RouteStatus execute() noexcept;
    void disarm() noexcept;
    void destroy() noexcept;
    ghostlock::route::RouteStatus status{};
};
```
源文件里（Production-only 放在 `#if defined(__ANDROID__)`）包含三样：
```cpp
#if defined(__ANDROID__)
#include "route/exploit_procedure.hpp"
// 1) route 级 PI 入口
namespace ghostlock::route {
    RouteStatus do_foo_fake_lock_route(const memory::WriteRequest *request) {
        FooRoute context(&session::g_exploit_session.race, request,
                         session::g_exploit_session.profile);
        return run_route_lifecycle(context);
    }
} // namespace ghostlock::route

// 2) Procedure 子类（只在需要覆盖 W1/W2/W3 钩子时）
namespace {
    class FooProcedure final : public ghostlock::session::ExploitProcedure {
    public:
        explicit FooProcedure(ghostlock::session::ExploitSession &s) : ExploitProcedure(s) {}
    protected:
        // 按需覆盖 resident_write / w1_attempt_cap / w2_fast_repair_* /
        // w1_scratch_repair / w1_resident_repair / w3_exact_target
    };
}

// 3) 工厂函数（工厂通过它创建，无需暴露 Procedure 类型）
namespace ghostlock::route {
    std::unique_ptr<ghostlock::session::ExploitProcedure> make_foo_procedure(
        ghostlock::session::ExploitSession &session) {
        return std::make_unique<FooProcedure>(session);
    }
} // namespace ghostlock::route
#endif // __ANDROID__
```
若与现有 route 行为一致，可直接复用已有 Policy/Procedure，只加 Route 类。

### 2.4 `route/route_api.hpp` — 声明 do_ 与 make_foo_procedure
```cpp
RouteStatus do_foo_fake_lock_route(const ghostlock::memory::WriteRequest *request);
std::unique_ptr<ghostlock::session::ExploitProcedure> make_foo_procedure(
    ghostlock::session::ExploitSession &session);
```

### 2.5 `route/exploit_procedure.cpp` — 工厂加 1 个 case
```cpp
case profile::RouteKind::FooWaiter: return route::make_foo_procedure(session);
```

### 2.6 传输字段（GLK1 v4：公共槽 + 按 route 扩展节）
- **route 专属字段**：进该 route 的扩展节，**不动公共 schema**
  - native `profile/binary.cpp`：`kXxxFields[]`（name→Field）并注册到 `kRouteFields[]`
  - Kotlin `data/NativeProfile.kt`：新增 `FooConfig : RouteConfig`，接 `routeEntries()`/`applyRouteEntry()`/`emptyRouteConfig()`/`from()`
  - `profile/model.h` `kernel_offsets` 加字段（纯数据槽；攻击代码读它）
- **route 无关字段**：进公共槽
  - native `kCommonFields[]`；Kotlin `flattenCommon()`/`fromCommon()`（两侧**顺序必须一致**）
- 键名两侧必须逐字一致；`version` 已是 4（未进生产，v4 与 v3 不兼容）

### 2.7 构建
`src/Makefile` `CXX_SRCS` 与 `src/CMakeLists.txt` 各加 `core/route/foo_route.cpp`（procedure 已折入，不再单独加）。

---

## 3. 测试与门禁

- `src/core/tests/route_catalog_test.cpp`：canonical 列表加 `("foo_waiter", 4)`
- 新增 `src/core/tests/foo_route_test.cpp`（构造/析构/几何，参考 `tcp_zerocopy_route_test.cpp`），并登记到 `NATIVE_HOST_TESTS` 与规则
- `src/core/tests/route_controller_test.cpp`：为新 route 提供 `do_foo_fake_lock_route` stub（控制器测试会引用所有 policy 的 `run`）
- Kotlin `RouteCatalogAgreementTest`：加同一 canonical 列表
- 新增攻击路径函数加入 `tools/cmp_disasm.py` 的 `TARGETS`
- 验证：`make ghostlock` / `make native-host-tests` / `make lint-tidy` / `cmp_disasm.py` / 真机门禁

---

## 4. 检查清单

- [ ] Kotlin `RouteKind` 加 `(token, wire)`
- [ ] Kotlin `RouteCatalogAgreementTest` 更新 canonical 列表
- [ ] Kotlin `FooConfig : RouteConfig`（route 专属参数）+ UI 标签/`.conf` 示例（可选）
- [ ] Native `RouteKind`（+ `kRouteFooWaiter`、`kRouteCatalog`）
- [ ] Native `FooPolicy` + 追加到 `RoutePolicyList`（1 行）
- [ ] Native `route/foo_route.{h,cpp}`：Route 类 + `do_foo_fake_lock_route` + `FooProcedure` + `make_foo_procedure`
- [ ] Native `route/route_api.hpp` 声明 `do_foo_fake_lock_route` 与 `make_foo_procedure`
- [ ] Native `make_exploit_procedure` 加 case
- [ ] route 专属字段进 route 扩展节（`kRouteFields` + `routeEntries`/`applyRouteEntry`）；公共字段进公共槽（两侧顺序一致）；**legacy 不动**
- [ ] `Makefile` / `CMakeLists.txt` 加 `core/route/foo_route.cpp`
- [ ] host test + `cmp_disasm` TARGETS + 真机门禁
