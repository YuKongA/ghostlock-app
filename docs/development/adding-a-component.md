# 如何新增组件（middleware / backend / frontend）

GhostLock 的执行链由 `Pipeline<Frontend, Backend, Middleware>` 在编译期固定：

- **frontend**：启动/交接（当前 `root_child`；`umh_forward` 为声明但不可用的占位）；
- **backend**：漏洞原语与写入步骤（当前 `cve_2026_43499`；`cve_2026_64560` 为纯头占位，不可用）；
- **middleware**：一次写的 route（`select_stack` / `tcp_zerocopy` / `multicast_waiter`）。

组件如何被选择、组合与执行，由以下唯一权威决定：

- 可用性：`route/component_catalog.hpp` 的 `frontend_available` / `backend_available` /
  `middleware_available`。identity 与执行 policy **都不携带 `available`**。
- 组合与分派：`combination_supported(selection)` 列出可派发三元组；`dispatch_target_of(frontend,
  backend, middleware)` 给出**完整组合**的分派目标；`orchestrator.hpp` 的每个 case 用
  `static_assert(P::target == …)` 锁定。**分派键保留完整选择**：当前目录只有
  `root_child × cve_2026_43499 × {三种 middleware}`，新增 frontend/backend 时必须同时扩展
  `DispatchTarget` 枚举、映射与 orchestrator 分支——不能只按 middleware 决定 pipeline。
- 执行入口：`route/pipeline.hpp` 的 `Pipeline<F,B,M>::run`，编译期校验 `catalogued`、
  `MiddlewarePolicy<M>`、`FrontendExecution<F>` 与 `BackendExecution<B,M>`，返回 `RunResult`。
- 绑定方式：backend 步骤模板化在 middleware 上（`Cve2026_43499Policy::run<M>` / `attack_write<M>`），
  以 `M::resident_write` 等静态 hook 直接调用，无虚表；hook 边界 `[[gnu::noinline]]`。

新增组件属 L 级改动：先读 `design-philosophy.md` 与 `engineering-standards.md`，产出计划并获认可，
再按本文改动。

---

## 一、新增 middleware（route）

### 改动一览

| 位置 | 文件 | 做什么 |
|---|---|---|
| Native 注册 | `profile/model.h` | `RouteKind` 取值、`kRouteFooWaiter`、`kRouteCatalog` 一行 |
| Native 可用性 | `route/component_catalog.hpp` | `middleware_available` 收录新 kind |
| Native Policy | `route/route_policy.hpp` | `FooPolicy`（能力 + `supported`/`run` + 需要的 hook 覆写声明），追加到 `RoutePolicyList` |
| Native 分派 | `route/component_catalog.hpp` + `route/orchestrator.hpp` | `DispatchTarget` 加值、`dispatch_target_of` 映射、orchestrator case（`Pipeline<…, FooPolicy>` + target `static_assert`） |
| Native 实例化 | `session/backend/cve_2026_43499_backend.cpp` | 显式实例化 `run<FooPolicy>` / `attack_write<FooPolicy>` |
| Native 实现 | `route/foo_route.{h,cpp}` | Route 类 + `do_foo_fake_lock_route` + policy hook 的 Android 定义 |
| Native 构建 | `src/Makefile`、`src/CMakeLists.txt` | 加入 `core/route/foo_route.cpp` |
| Native 传输 | `profile/binary.cpp` | route 字段表（有私有参数时） |
| Native 导出 | `build.gradle.kts` | `routeFieldPaths` 加该 route |
| Kotlin 注册 | `data/route/RouteKind.kt` | `(token, wire)` 与配置构造 |
| Kotlin 参数 | `data/route/FooConfig.kt` | 有私有参数时新增 |
| 测试 | 见「四」 | catalog/policy/catalog-dispatch/contract/route/cmp_disasm |

### 1. 注册 route

两侧登记同一个 token 与 wire 值。`src/core/tests/route_catalog_test.cpp` 与
`app/src/test/.../RouteCatalogAgreementTest.kt` 断言同一列表，任何一边漏改都会失败。

### 2. 私有参数（可选）

只有这条 route 才用的参数放 route 扩展节：Kotlin `FooConfig` 的 `entries()/apply()/from()`
与 native `profile/binary.cpp` 的字段表键名逐字一致；导出任务 `routeFieldPaths` 同步。
共享代码会读的参数属于公共槽，两侧顺序必须一致。

### 3. Route 本体

`route/foo_route.{h,cpp}`：Route 类满足 `RouteLifecycle`（`prepare → execute → disarm → destroy`，
仅经 `status` 汇报；不用虚基类）。上半部分 host 可编译，入口与 hook 定义放 `#if defined(__ANDROID__)` 段。

### 4. Policy 与 hook

`route/route_policy.hpp` 加 `FooPolicy : RoutePolicyDefaults`：

- **能力**（`static constexpr bool`，编译期）：`multicast`、`w2_fast_repair`、`w3_exact_target`、
  `tcp_payload_layout`、`allows_fallback`。只写需要为 `true` 的，其余继承默认。
- **有副作用的 hook**：`resident_write(session, request)`、`w1_resident_repair(session)`、
  `w2_fast_repair_prebuild/activate(session)`。默认由 `RoutePolicyDefaults` 提供；覆写时在该 unit
  提供定义并让声明带 `[[gnu::noinline]]`（防止 LTO 把 route 实现内联进攻击函数）。
- **纯查询**（如一次性 route 的 W1 重试上限、是否需要 scratch 修复、W3 是否精确命中）：不需要 hook，
  在 backend 步骤内用 `if constexpr (M::…)` + profile 运行时值表达。

### 5. 分派与实例化

catalog 加 `DispatchTarget` 值与该 kind 的映射；orchestrator 加 case：

```cpp
case DispatchTarget::RootChild_Cve43499_FooWaiter: {
    using P = Pipeline<session::frontend::RootChildPolicy,
                       session::backend::Cve2026_43499Policy,
                       route::FooPolicy>;
    static_assert(P::target == DispatchTarget::RootChild_Cve43499_FooWaiter,
                  "dispatch case must match the pipeline's target");
    return P::run(exploit_session, decoded, debug_dir, force_attack);
}
```

并在 `cve_2026_43499_backend.cpp` 末尾补该 policy 的显式实例化（漏掉会链接失败）。
`DispatchTarget` 名带完整组合前缀，是为了让后端/前端维度扩展时映射与分支同步增长。

---

## 二、新增 backend

| 位置 | 文件 | 做什么 |
|---|---|---|
| identity | `route/backend_policy.hpp` | `FooBackend` + 与 catalog 绑定的 `static_assert` |
| 可用性/组合 | `route/component_catalog.hpp` | `BackendKind` 取值、`backend_available`、`combination_supported`、分派维度 |
| contract 注册 | `route/backend_contract.hpp` | `BackendIdentityList` 加 `FooBackend` |
| 执行 policy | `session/backend/foo_backend.{hpp,cpp}` | `FooPolicy`（`kind` + `run<M>` / `attack_write`）、identity 绑定断言、显式实例化 |
| 分派 | `route/orchestrator.hpp` | 新 backend × middleware 组合的 `Pipeline` 分支 |
| 构建/测试 | `src/Makefile` + `backend_contract_test` + `profile_binary_test` + Kotlin | |

**不可用 backend（占位）**：只提供 identity + `kind` + `unavailable_reason`（参考
`session/backend/cve_2026_64560_backend.hpp`），不实现 `run`、不实例化 `Pipeline`；可用性由 catalog
表达，wire 只携带 id（没有 backend 私有 section——未来字段必须进自己的 section，不得复用 43499 的槽）。

---

## 三、新增 frontend

与 backend 同构：`route/frontend_contract.hpp` 提供 `FrontendIdentity`（仅 `kind`）与
`FrontendExecution<F>`（可用 frontend 的 `run(session, chain)` 精确返回 `StageResult`），以及
`FrontendIdentityList` / `for_each_frontend` 注册表；执行 policy 在 `session/root_child_frontend.hpp`
（`RootChildPolicy::run` 承载 startup/handoff），该头以 `static_assert` 绑定 identity 与 catalog；
`Pipeline` 以 `static_assert(FrontendExecution<F>)` 约束组合入口。UMH 的职责边界见
`frontend_contract.hpp`（child 生命周期与 KernelSU handoff 不合并；UMH 不得默认绑定 KernelSU）。

---

## 四、构建、测试与门禁

新增组件至少补这些测试：

1. `route_catalog_test.cpp` / `RouteCatalogAgreementTest.kt`：canonical 列表同步（middleware）。
2. `route_policy_test.cpp`：能力断言与派发计数（middleware）。
3. `component_catalog_test.cpp`：组合、`dispatch_target` 与映射（新组合）。
4. `backend_contract_test.cpp`：`FrontendIdentity` / `FrontendExecution` / `BackendIdentity` /
   `BackendExecution` / `Pipeline::target`（新 frontend/backend/middleware）。
5. `foo_route_test.cpp`：构造/析构/几何（middleware），并登记进 `NATIVE_HOST_TESTS`。
6. `profile_binary_test.cpp`：wire 解码/拒绝（新 id）。
7. 新增攻击路径函数时，把它加进 `tools/cmp_disasm.py` 的 `TARGETS`。

```sh
make -C src ghostlock            # 真机二进制
make -C src native-host-tests    # 主机单元测试
make -C src lint-tidy            # clang-tidy（0 findings）
python3 tools/cmp_disasm.py <baseline> build/native/ghostlock
./gradlew :app:testDebugUnitTest --offline
```

最后在真机跑一次（冷机、固定 CPU 对、KernelSU 未加载），确认组件被选中、写验证通过，
并按 `docs/analysis/device-gates/` 格式归档。未过真机不得标 supported。

---

## 核对清单

新增 middleware：

- [ ] `RouteKind` + `kRouteCatalog`（两侧一致）
- [ ] `middleware_available` 收录
- [ ] `FooPolicy`（能力 + hook）+ `RoutePolicyList`
- [ ] `DispatchTarget` + `dispatch_target_of` + orchestrator case（target 断言）
- [ ] `cve_2026_43499_backend.cpp` 显式实例化
- [ ] Route 类 + 入口 + Android hook 定义
- [ ] `Makefile` / `CMakeLists.txt` / `routeFieldPaths` / route 扩展节
- [ ] 测试矩阵 + `cmp_disasm`（触攻击路径时）+ 真机门禁

新增 backend：

- [ ] identity + catalog 可用性/组合 + `BackendIdentityList`
- [ ] 执行 policy（`run<M>` / 显式实例化 / identity 绑定断言）或占位（无 run）
- [ ] orchestrator 组合分支 + 测试 + wire 解码测试

新增 frontend：

- [ ] identity（可含 unavailable reason）+ 执行 policy（handoff）+ 组合接线 + 测试
