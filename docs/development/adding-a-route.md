# 如何新增一条攻击链（route）

GhostLock 把"把一个值写进内核内存"的每一种实现方式称为一条 **route**。
当前有三条：`select_stack`、`tcp_zerocopy`、`multicast_waiter`。
本文以新增一条假想的 `foo_waiter` 为例，带你走完从注册到测试的全部改动。

**适用读者**：能构建本项目（Native 与 App 都能跑起来），想添加一条新攻击路径的开发者。
如果你只是给已有 route 加一个可调参数，只需要看第 2 步和第 6 步。

## 先理解三件事

写代码之前，先知道这套结构为什么长这样，后面每一步就不容易做错。

1. **表现和能力归 Policy，流程差异归 Procedure。**
   每条 route 有一个 Policy，它声明这条 route 的编译期能力（是否需要收尾、W2 是否要修复、W3 是否精确命中……），
   并指向自己的入口函数。Procedure 则是在整条攻击流程（setup → W1 → W2/W3 → handoff）中，
   这条 route 与别人不同的那几个步骤。两者分开，是为了让共享流程始终只有一份。

2. **Policy 必须能被主机编译，Procedure 只能在 Android 上编译。**
   主机的单元测试会编译所有 Policy（`route_controller_test` 会引用每个 Policy 的入口），
   所以 Policy 不能碰只有真机才有的东西。Procedure 依赖真实的 session 状态，
   因此它和 route 本体放在同一个 `.cpp` 里，并用 `#if defined(__ANDROID__)` 包住。

3. **route 的私有参数走 v2 profile 的 route 扩展节。**
   公共字段是所有 route 都要用的；只有这条 route 才需要的参数，放进它自己的扩展节。
   判断标准是"谁读它"，不是名字看起来像谁：只要共享代码会读，就必须留在公共槽。

## 改动一览

下表是全部触点，后面按顺序展开。

| 位置 | 文件 | 做什么 |
|---|---|---|
| Native 注册 | `profile/model.h` | 加 `RouteKind` 取值、`kRouteFooWaiter`、`kRouteCatalog` 一行 |
| Native Policy | `route/route_policy.hpp` | 加 `FooPolicy`，追加到 `RoutePolicyList` |
| Native 实现 | `route/foo_route.{h,cpp}` | Route 类、入口函数、Procedure、工厂 |
| Native 声明 | `route/route_api.hpp` | 声明入口函数与工厂 |
| Native 工厂 | `route/exploit_procedure.cpp` | `make_exploit_procedure` 加一个 case |
| Native 传输 | `profile/binary.cpp` | 注册 route 字段表（有私有参数时） |
| Native 构建 | `Makefile`、`CMakeLists.txt` | 加入 `core/route/foo_route.cpp` |
| Native 导出 | `build.gradle.kts` | `exportKernelProfiles` 加 `routeFieldPaths` |
| Kotlin 注册 | `data/route/RouteKind.kt` | 加 `(token, wire)`、空配置、构造器 |
| Kotlin 参数 | `data/route/FooConfig.kt` | 有私有参数时新增 route 配置类型 |
| Kotlin 测试 | `RouteCatalogAgreementTest.kt` | 加入同一个 canonical 列表 |

> 新 route 不涉及 v1（旧 JSON）路径。`legacy/offsets_json.cpp` 与 `LegacyProfileConverter.kt` 保持不动。

---

## 第 1 步：注册 route

先在两侧登记同一个 token 和 wire 值。这一步做完就能通过一致性测试，但 route 还跑不起来。

### Native

`src/core/profile/model.h`：

```cpp
enum class RouteKind : uint8_t {
    Auto = 0, TcpZerocopy = 1, SelectStack = 2, MulticastWaiter = 3, FooWaiter = 4,
};
inline constexpr uint8_t kRouteFooWaiter = static_cast<uint8_t>(RouteKind::FooWaiter);

inline constexpr RouteCatalogEntry kRouteCatalog[] = {
    {"tcp_zerocopy", kRouteTcpZerocopy},
    {"select_stack", kRouteSelectStack},
    {"multicast_waiter", kRouteMulticastWaiter},
    {"foo_waiter", kRouteFooWaiter},        // 新增
};
```

`route_kind_from_string()` 和 `kernel_offsets::route_kind()` 会自动读这张表，不用改。

### Kotlin

`app/src/main/kotlin/com/ghostlock/app/data/route/RouteKind.kt`：

```kotlin
FOO_WAITER(4, "foo_waiter", FooConfig.EMPTY, { value -> FooConfig.from(value) }),   // 新增
```

如果这条 route 暂时没有私有参数，先用 `NoRouteConfig` 占位，等第 2 步再换成自己的配置类型。

### 别让它漂移

`src/core/tests/route_catalog_test.cpp` 和
`app/src/test/kotlin/com/ghostlock/app/data/RouteCatalogAgreementTest.kt`
各自断言同一份 `(token, wire)` 列表。任何一边忘了改，测试就会失败——这是故意的。

---

## 第 2 步：给 route 加私有参数（可选）

如果 `foo_waiter` 需要自己的调参（比如一个等待时长），按下面的方式加。
如果不需要，跳过这一步。

### Kotlin：一个 route 一个配置文件

新建 `app/src/main/kotlin/com/ghostlock/app/data/route/FooConfig.kt`：

```kotlin
internal data class FooConfig(val armUs: Long) : RouteConfig {
    override fun entries(): List<Pair<String, Long>> = listOf("foo_arm_us" to armUs)

    override fun apply(key: String, value: Long): RouteConfig =
        if (key == "foo_arm_us") copy(armUs = value) else this

    companion object {
        val EMPTY = FooConfig(0L)
        fun from(value: (String) -> Long): FooConfig =
            FooConfig(armUs = value("execution.routes.foo_waiter.arm_us"))
    }
}
```

`entries()` 负责写二进制，`apply()` 负责读二进制，`from()` 负责从 HOCON 的
`execution.routes.foo_waiter.arm_us` 取值。三处的键名必须和 Native 完全一致。

**不要**把私有参数塞进共享的 `NativeProfileDocument` 或 `ExecutionTuning`。

### Native：注册 route 字段表

在 `src/core/profile/binary.cpp` 里定义这张 route 的字段表，并注册进 `kRouteFields[]`：

```cpp
constexpr NamedField kFooFields[] = {
    {"foo_arm_us", FIELD(execution.foo_arm_us)},
};

constexpr RouteFields kRouteFields[] = {
    {profile::kRouteTcpZerocopy, kTcpFields, std::size(kTcpFields)},
    {profile::kRouteSelectStack, kSelectFields, std::size(kSelectFields)},
    {profile::kRouteMulticastWaiter, kMulticastFields, std::size(kMulticastFields)},
    {profile::kRouteFooWaiter, kFooFields, std::size(kFooFields)},   // 新增
};
```

如果参数是普通的 `kernel_offsets` 成员（而不是 `execution` 里的），再给 `kernel_offsets`
加一个数据槽，并在 `TargetProfile` 上补一个类型化 getter，这样调用点不用手动转型。

### 什么时候该放公共槽

再强调一次判断标准：**谁读它**。
如果这个参数会被共享代码读取（例如 consumer 线程、payload 构造），它属于公共槽，
应该加进 `kCommonFields` 和 Kotlin 的 `flattenCommon()`/`fromCommon()`，两侧顺序必须逐字一致。
把它放进 route 扩展节，其他 route 就会读到 0，这类问题在真机上表现为"路由此刻无法验证"。

### 导出也要同步

GitHub 发布的 `GhostLock-kernel-profiles.zip` 由根目录的 `exportKernelProfiles` 任务生成。
在 `build.gradle.kts` 的 `routeFieldPaths` 里加上这条 route 的键和对应的 HOCON 路径，
否则导出的 `.bin` 会缺字段。

---

## 第 3 步：实现 route 本体

在 `src/core/route/` 下新建 `foo_route.h` 和 `foo_route.cpp`。头文件只声明 Route 类：

```cpp
namespace ghostlock::route::foo_waiter {
    class FooRoute final {
    public:
        FooRoute(ghostlock::race::PiRace *race,
                 const ghostlock::memory::WriteRequest *request,
                 const ghostlock::profile::TargetProfile &profile) noexcept;

        FooRoute(const FooRoute &) = delete;
        FooRoute &operator=(const FooRoute &) = delete;

        [[nodiscard]] int32_t prepare() noexcept;          // 返回 0 表示就绪
        [[nodiscard]] ghostlock::route::RouteStatus execute() noexcept;
        void disarm() noexcept;
        void destroy() noexcept;

        ghostlock::route::RouteStatus status{};
    };
} // namespace ghostlock::route::foo_waiter
```

Route 类要满足 `RouteLifecycle` 这个概念（见 `route/route_lifecycle.hpp`）：
固定顺序是 `prepare → execute`（仅在 prepare 返回 0 时）`→ disarm → destroy`，
最后由 `status` 汇报结果。这里刻意不用虚基类，因为整段生命周期跑在 PI 竞争窗口内，
不能有间接调用。

`foo_route.cpp` 分两段：上半部分是 Route 类实现（主机也能编译），
下半部分用 `#if defined(__ANDROID__)` 包住入口函数、Procedure 和工厂。

---

## 第 4 步：接入攻击流程

### 4.1 Policy：声明能力和入口

在 `route/route_policy.hpp` 里加：

```cpp
struct FooPolicy : RoutePolicyDefaults {
    static constexpr RouteKind kind = RouteKind::FooWaiter;
    // 只写需要为 true 的能力，其余继承基类的 false，例如：
    // static constexpr bool multicast = true;
    // static constexpr bool w2_fast_repair = true;
    // static constexpr bool w3_exact_target = true;
    // static constexpr bool tcp_payload_layout = true;
    // static constexpr bool allows_fallback = true;

    static bool supported(const profile::TargetProfile &profile) noexcept {
        return profile.supports(kind);
    }

    static RouteStatus run(const memory::WriteRequest *request) {
        return do_foo_fake_lock_route(request);
    }
};

using RoutePolicyList = std::tuple<SelectPolicy, TcpPolicy, MulticastPolicy, FooPolicy>;  // 追加
```

`RoutePolicyDefaults` 提供了所有能力的默认值。以后新增一种能力时，只需在基类加一个默认 `false`，
已有 Policy 不用动；需要的 Policy 再重声明为 `true` 即可。这些能力都是编译期常量，
`route_capability` 会用直接比较读取，不会引入间接分派。

这些能力的含义：

| 能力 | 作用 | 谁在用 |
|---|---|---|
| `multicast` | 收尾是否需要 ghost disarm | `race/threads.cpp` |
| `tcp_payload_layout` | payload 是否用 tcp 几何 | `support/util.cpp` |
| `w2_fast_repair` | W2 是否预热/激活修复 | `ExploitProcedure` |
| `w3_exact_target` | W3 是否跳过 leaf 探测 | `ExploitProcedure` |
| `allows_fallback` | 是否允许 profile 声明的 fallback | `run_route_policy` |

### 4.2 入口函数、Procedure、工厂

在 `foo_route.cpp` 的 `#if defined(__ANDROID__)` 段里写三样东西：

```cpp
#include "route/exploit_procedure.hpp"
#include "route/route_lifecycle.hpp"

// 1) route 入口：构造 Route 并跑固定生命周期
namespace ghostlock::route {
    RouteStatus do_foo_fake_lock_route(const memory::WriteRequest *request) {
        foo_waiter::FooRoute context(
            &session::g_exploit_session.race, request,
            session::g_exploit_session.profile);
        return run_route_lifecycle(context);
    }
} // namespace ghostlock::route

// 2) Procedure：只覆盖与本 route 不同的步骤，行为一致就全部省略
namespace {
    class FooProcedure final : public ghostlock::session::ExploitProcedure {
    public:
        explicit FooProcedure(ghostlock::session::ExploitSession &session)
            : ExploitProcedure(session) {}

    protected:
        // 可覆盖的钩子见下表
    };
} // namespace

// 3) 工厂：Procedure 类型不外泄
namespace ghostlock::route {
    std::unique_ptr<ghostlock::session::ExploitProcedure> make_foo_procedure(
        ghostlock::session::ExploitSession &session) {
        return std::make_unique<FooProcedure>(session);
    }
} // namespace ghostlock::route
```

`ExploitProcedure` 提供的钩子（默认都是中性行为）：

| 钩子 | 作用 | 覆盖示例 |
|---|---|---|
| `resident_write(request)` | 写内常驻快路径；返回 `nullopt` 表示不接管 | multicast |
| `w1_attempt_cap(base)` | 限制 W1 重试次数（一次性 route 只能 1 次） | multicast |
| `w2_fast_repair_prebuild()/activate()` | W2 凭据写的预建/激活修复 | multicast |
| `w1_scratch_repair()` | W1 后修复私有 scratch 页 | multicast |
| `w1_resident_repair()` | W1 后修复常驻 policycap | multicast |
| `w3_exact_target()` | W3 是否精确命中（跳过 leaf 探测） | tcp |

`run()`、`setup`、`w1`、`handoff`、`attack_write`、`retry_write_stage` 是共享步骤，
不要在各 route 里复制它们。

### 4.3 声明与工厂注册

`route/route_api.hpp`：

```cpp
RouteStatus do_foo_fake_lock_route(const memory::WriteRequest *request);

std::unique_ptr<ghostlock::session::ExploitProcedure> make_foo_procedure(
    ghostlock::session::ExploitSession &session);
```

`route/exploit_procedure.cpp` 的 `make_exploit_procedure` 里加：

```cpp
case profile::RouteKind::FooWaiter: return route::make_foo_procedure(session);
```

`default` 分支会对未登记的 route 抛 `FatalError{}`，所以忘记加 case 会在运行时报错，
不会静默跑错路线。

---

## 第 5 步：构建

把新文件加进两处构建脚本：

- `src/Makefile` 的 `CXX_SRCS`
- `src/CMakeLists.txt`

都加 `core/route/foo_route.cpp`。Procedure 已经在同一个文件里，不用再加别的。

Kotlin 侧不用改构建脚本。

---

## 第 6 步：测试

新增 route 至少要补这些测试：

1. `src/core/tests/route_catalog_test.cpp`：canonical 列表加 `{"foo_waiter", 4}`。
2. `src/core/tests/foo_route_test.cpp`：构造/析构/几何，可参考 `tcp_zerocopy_route_test.cpp`。
   在 Makefile 的 `NATIVE_HOST_TESTS` 和编译规则里登记。
3. `src/core/tests/route_controller_test.cpp`：为 `do_foo_fake_lock_route` 提供 stub。
   主机编译 `route_controller.cpp` 时会引用每个 Policy 的入口，少了会链接失败。
4. Kotlin `RouteCatalogAgreementTest`：加同一行 canonical 列表。
5. 如果新增了攻击路径函数，把它加进 `tools/cmp_disasm.py` 的 `TARGETS`。

然后依次跑：

```sh
make -C src ghostlock            # 构建真机二进制
make -C src native-host-tests    # 主机单元测试
make -C src lint-tidy            # clang-tidy
python3 tools/cmp_disasm.py <baseline> build/native/ghostlock
./gradlew :app:testDebugUnitTest --offline
```

最后在真机上跑一次，把日志（`Download/GhostLock/<时间>/` 下的 `*.log.txt`）确认 route 确实被选中、
攻击写验证通过。非字节一致的改动必须过真机验证。

---

## 核对清单

- [ ] Native `RouteKind` + `kRouteFooWaiter` + `kRouteCatalog` 一行
- [ ] Native `FooPolicy : RoutePolicyDefaults` + 追加到 `RoutePolicyList`
- [ ] Native `route/foo_route.{h,cpp}`：Route 类 + 入口 + Procedure + 工厂
- [ ] Native `route/route_api.hpp` 声明两个符号
- [ ] Native `make_exploit_procedure` 加 case
- [ ] Native route 扩展节字段表（有私有参数时）+ 公共槽顺序两侧一致
- [ ] `Makefile` / `CMakeLists.txt` 加 `core/route/foo_route.cpp`
- [ ] `build.gradle.kts` `exportKernelProfiles` 的 `routeFieldPaths` 加该 route
- [ ] Kotlin `RouteKind.kt` 加项
- [ ] Kotlin `data/route/FooConfig.kt`（有私有参数时）
- [ ] 两侧一致性测试同步
- [ ] host test + `cmp_disasm` + 真机验证
