# Route 配置可插拔化计划（Kotlin + native）

目标：新增/修改一条 route 时，只改**一处 route 描述**，不需要在两侧多个 `when`/`switch`/`case` 里补分支。
现状 route 逻辑散落在 Kotlin 与 native 的多个硬编码点，本文件盘点并给出目标设计。

## 1. 现状：route 出现点

### Native
- `profile/model.h`：`RouteKind` 枚举、`kRouteCatalog`（token↔wire，**已加**）、`TargetProfile::route()/fallback_route()`
- `route/route_policy.hpp`：`RoutePolicyList`（`Select/Tcp/MulticastPolicy`，含 `kind` + `run()` + capability）
- `route/exploit_procedure.cpp`：`make_exploit_procedure` 按 `RouteKind` switch 选 procedure
- `route/route_controller.cpp` + `run_route`：按 policy 选择/fallback（已数据驱动）
- 解码器 `legacy/offsets_json.cpp` / `profile/binary.cpp`：按 route 解析不同几何（`kRouteMulticastWaiter` case、`SCALAR_ALIAS` 等）
- `route/route_lifecycle.hpp`：`RouteLifecycle` concept
- 各 route 类：`multicast_waiter_route` / `tcp_zerocopy_route` / `select_stack_route`

### Kotlin
- `data/Profile.kt`：`enum class RouteKind(wire, token)`（**唯一路由目录**）+ `fromToken/fromWire`
- `data/NativeProfile.kt`：`routeKind(route)` 原为独立 `when`（**已改为 `RouteKind.fromToken`**）；execution 的 `execution.routes.<token>.*` 键
- `data/LegacyProfileConverter.kt`：按 route 搬特定几何字段（`tcp_zerocopy`/`select_stack`/`multicast_waiter` 三个 `when` 分支）
- `ui/FieldLabels.kt`、`BuiltinProfileCatalog.kt`、UI：route 展示/选择
- 若干 `kernel_profiles/*.conf`：route 块 + `to = "<route>"` fallback

## 2. 目标设计

### 2.1 每侧一个 Route 目录（catalog）
- Native：`kRouteCatalog`（token↔wire）+ `RoutePolicyList`（kind→行为）。二者用 `kind` 关联，新增 route = 加 1 目录项 + 1 个 policy + 1 个 procedure。
- Kotlin：`RouteKind` enum 即目录（token/wire/label）。新增 route = 加 1 个 enum 项。

### 2.2 每 route 一个配置解码器（消除解码 switch）
- Native：把 `offsets_json`/`binary` 里按 `RouteKind` 的几何解析，改成「每个 route 的 `Decoder` 由一个 `for_each_policy` 式注册表分派」；无法纯数据化的（multicast 专属几何）用 per-route `decode()` 钩子。
- Kotlin：把 `LegacyProfileConverter` 里三个 `when` 分支改为 `Map<RouteKind, LegacyRouteCodec>`（每 route 一个 codec 对象）。

### 2.3 execution route 键
- Kotlin：`execution.routes.<token>.*` 已按 token 拼键；改为 `RouteKind.executionKeys`（或 per-route `ExecutionCodec`）。
- Native：`execution_settings` 按字段名解码；route 专属字段可归到 per-route decoder。

### 2.4 工厂/选择
- Native `make_exploit_procedure` 的 switch：改为由 `RoutePolicyList` + `kRouteCatalog` 驱动的查表（`route_kind_from_string` 已数据化）。
- Kotlin 无需工厂（route 只是 token/wire）。

## 3. 跨语言一致性

两种方案：
- **A（推荐，低基础设施）**：两侧各自保留 catalog，各加一个**一致性测试**（Kotlin 单测断言 `RouteKind.values()` 的 token/wire 集合等于 canonical 列表；native host test 断言 `kRouteCatalog` 等于同一列表）。漂移即测试失败。
- **B（单一真源）**：放一个 `route_catalog.txt/json` 资源，Kotlin 与 native 构建期/运行期读取并生成/校验。基础设施成本高。

## 4. 进度

- [x] Native：`profile/model.h` 增 `kRouteCatalog`，`route_kind_from_string` 由目录驱动
- [x] Kotlin：`NativeProfile.routeKind` 去掉重复 `when`，改用 `RouteKind.fromToken`（Kotlin 侧目录唯一）
- [ ] Native：解码器 per-route 钩子（`offsets_json`/`binary`）
- [ ] Kotlin：`LegacyProfileConverter` per-route codec；execution route 键由 `RouteKind` 派生
- [ ] 两侧一致性测试（方案 A）
- [ ] UI/`FieldLabels` route 展示由目录驱动

## 5. 验证

- Native：`make ghostlock` / `make native-host-tests` / `make lint-tidy` 通过（`kRouteCatalog` 改动后产物 `384e7eed…`，非字节一致，需真机门禁）
- Kotlin：改动为一行等价替换（`RouteKind.fromToken`），需在本机跑 `./gradlew :app:testDebugUnitTest`

## 6. 进度续（2026-09-22）

- [x] Kotlin：`LegacyProfileConverter` 的路由 `when`（branch + fallback）→ `Map<String, LegacyRouteCodec>`（每 route 一个 codec：`TcpRouteCodec`/`SelectRouteCodec`/`MulticastRouteCodec`），新增 route = 加 codec + 注册
- [x] 两侧一致性测试：
  - Native host test `core/tests/route_catalog_test.cpp`（断言 `kRouteCatalog` 与 canonical 列表一致）
  - Kotlin `app/src/test/.../RouteCatalogAgreementTest.kt`（断言 `RouteKind.values()` 同一列表）
- 验证：native `make native-host-tests` 通过；Kotlin `./gradlew :app:testDebugUnitTest --offline` 通过（含新测试）
- 注：本仓库位于同步盘，`build/` 里会冒出带 “ 2/3” 的冲突副本导致 Gradle 失败；跑前 `rm -rf build/app/{generated,intermediates}` 可解
- [ ] 待办：native `legacy/offsets_json.cpp`/`profile/binary.cpp` 的按 route 几何解码分支 → per-route 解码钩子

## 7. Native 解码钩子（2026-09-22）

- `legacy/offsets_json.cpp` 的 `apply_route_branch_values` 由 `switch(route_kind)` 改为 `kRouteBranchDecoders` 注册表（`route_branch_decoder{ kind, apply }`）：`decode_tcp_branch` / `decode_select_branch` / `decode_multicast_branch`；新增 route 分支 = 加一个 decoder 并注册
- `profile/binary.cpp` 无 route 几何分支（route 仅为 1 字节 + flat execution），无需改
- 验证：`make ghostlock` / `make native-host-tests` / `make lint-tidy` 通过。产物 `fb11b375…`（需真机门禁）
- 剩余（可选）：UI/`FieldLabels.kt` 的 route 展示改为由 `RouteKind` 目录驱动；execution route 键已按 token 拼键

## 8. A 方案收敛（2026-09-22）

- `RoutePolicyVariant` 由 `RoutePolicyList` 自动推导（去掉第二次列表维护）
- **Procedure 折进各 route 文件**（production-only，`#if defined(__ANDROID__)`）：
  - `route/select_stack_route.cpp`：`SelectProcedure` + `make_select_procedure`
  - `route/tcp_zerocopy_route.cpp`：`TcpProcedure` + `make_tcp_procedure`
  - `route/multicast_waiter_route.cpp`：`MulticastProcedure` + `make_multicast_procedure`
  - 删除 `route/{select,tcp,multicast}_procedure.{hpp,cpp}`（6 个文件）
- `route/route_api.hpp` 声明 `make_*_procedure`；`exploit_procedure.cpp` 工厂改调它们
- 说明：Policy 必须 host-safe（`route_controller.cpp` 被 host 编译），Procedure 依赖 common.h/session，故二者不能同文件；A 的上限即「Procedure 进 route 文件」
- 验证：`make ghostlock` / `make native-host-tests` / `make lint-tidy` 通过。产物 `1db4f517…`（需真机门禁）
- 开发指南已同步简化：`docs/analysis/adding-a-route.md`

## 9. GLK1 v4：公共槽 + route 扩展节（2026-09-22）

- 未进生产，GLK1 可直接改：v4 = 固定公共槽（route 无关字段）+ route 扩展节
  （`u8 count` → `u8 keylen + key + u64`），按 route 表解码；新增 route 只加自己的键，不改公共 schema
- native `profile/binary.cpp`：`kCommonFields` + `kXxxFields`（`kRouteFields` 注册）
- Kotlin `NativeProfile.kt`：`flattenCommon()` + `routeEntries()`/`applyRouteEntry()`；`Version = 4`
- route 专属字段（multicast 几何/`off_mcast_fake_bss`/resident、`pselect_waiter_shift`、tcp/select/multicast 的 execution 调参）从共享槽位移出，放入各自节
- **remote/main legacy 未动**（`legacy/offsets_json.cpp` + `LegacyProfileConverter.kt`）
- 验证：`make ghostlock` / `make native-host-tests`（含更新后的 `profile_binary_test`）/ `make lint-tidy` / `./gradlew :app:testDebugUnitTest --offline`（含更新后的 `ProfileRoundTripTest`）全通过
- 产物 `…`（需真机门禁；App 与 native 同版本，真机解析 App 生成的 profile 即验证）
