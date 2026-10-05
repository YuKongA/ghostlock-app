# 如何新增组件（backend / terminal / route / platform / ancillary）

> 现状权威：ADR-0001/0002/0004。执行链由 **`Pipeline<Backend, Terminal>`** 在编译期固定：
> 两个装配轴是 **backend**（漏洞原语与写入步骤）与 **terminal**（启动/交接）；route 是 backend
> **内部**策略，不是装配轴；`platform` 与 `ancillary` 是横切。旧的 frontend x backend x middleware
> 三轴模型、`file_write`/`panic` terminal 占位均已不存在，本文已按当前代码重写。

新增组件属 **L 级改动**：先读 `docs/development/design-philosophy.md` 与
`docs/development/engineering-standards.md`，产出计划并获认可，再按本文改动。

## 0. 权威分工（先记住，再动手）

| 关注点 | 唯一权威 | 说明 |
|---|---|---|
| kind、每轴可用性、identity、执行概念 | `contract/identity.hpp` | 声明型、必须 host 可编译；identity **不携带** `available` 状态 |
| 组合（wiring） | `pipeline/component_catalog.hpp` | 稀疏 `(backend, steps, terminal)` triple、`DispatchTarget`、`combination_supported`、`dispatch_target_of`；词汇来自 `contract` |
| 执行入口 | `pipeline/pipeline.hpp` | `Pipeline<Backend, Terminal>::run`，static_assert 编目 triple 与两个执行概念 |
| 分派 | `pipeline/orchestrator.hpp` | 按 `dispatch_target()` switch，每个 case 用 `Pipeline::target` static_assert 锁定 |
| 分层依赖 | `tests/include_firewall_test.cpp`（R1） | 越层 include 会 FAIL；白名单 stale 也 FAIL |

两个谓词回答不同问题，**不要混用**：

- `contract::selection_supported(selection)`：该选择是否**设备已核实、可以运行**（运行时 fail-closed 闸）。
- `pipeline::combination_supported(selection)`：该 triple 是否**已接线**（有编译期 Pipeline 与 orchestrator case）。
  已接线 != 可用；`cve_2026_43284 x pagecache_write x umh_forward` 就是"已接线、未可用"。

选择显式来自 profile/wire（GLKv3 的 `backend`/`terminal` 文本 token + backend 私有 section 的 `steps`），
不从 kernel 版本推断。新增组件优先不引入越层 include；确需临时豁免时在
`include_firewall_test.cpp` 的 `kWhitelist` 登记并写明 owner 批次。

---

## 一、新增 backend

后端提供漏洞原语、写入步骤与（可选）per-backend State。改动清单：

| 位置 | 文件 | 做什么 |
|---|---|---|
| kind + identity | `contract/identity.hpp` | `BackendKind` 取值、`namespace backend { struct Foo final { static constexpr BackendKind kind; }; }`、配对的 `static_assert` |
| 可用性 | `contract/identity.hpp` | `backend_available(Foo::kind)`；**只在真机门禁通过后**翻 true |
| 注册表 | `contract/identity.hpp` | `BackendIdentityList` 加 `backend::Foo` |
| 执行 policy | `backend/foo_backend.hpp`（+ `.cpp`） | `FooPolicy`：`kind`、`steps`、`state_from`、`run`；可选 `State`/`state_construct`/`state_destroy` |
| 组合 | `pipeline/component_catalog.hpp` | `combination_supported` 收录新 triple、`DispatchTarget` 加值、`dispatch_target_of` 映射、`backend_name`/`backend_from_token` |
| 分派 | `pipeline/orchestrator.hpp` | `Pipeline<FooPolicy, TerminalPolicy>` case + target `static_assert` |
| 构建 | `src/Makefile`、`src/CMakeLists.txt` | 加入新 `.cpp` |
| 测试 | `backend_contract_test.cpp`、`component_catalog_test.cpp` | identity/execution/state、catalog 与 target |
| wire | `backend/foo/glkv3_schema.hpp`（+ `schema.hpp`） | 私有字段（如有）；同步 manifest（见 §五） |
| Kotlin | `profile-core/.../component/ComponentKind.kt` | `BackendKind` 的 `(wire, token, available)`，available 与 native 对齐 |

要点：

- **identity 与执行分离**：`backend::Foo` 只声明 kind；`contract::backend_available` 是唯一可用性事实；
  `FooPolicy` 提供 `BackendExecution<FooPolicy, Terminal::Input>`。需要 State 的 backend 额外满足
  `BackendState`，`Pipeline` 用 RAII 在每条退出路径上 `state_destroy`。
- **占位 backend**：只声明 identity + kind，不实现 `run`、不实例化 `Pipeline`，可用性由
  `backend_available` 保持 false（参考其余 CVE 头）。wire 只携带 id，私有字段必须进自己的 section。
- **已接线但未可用**：若某 backend 需要先落执行 policy 与组合覆盖（如 B5-8 的 43284），
  在 catalog 登记 triple、orchestrator 落地 case，但 `backend_available` 保持 false；
  `component_catalog_test` 断言 `catalogued == 3` 等既有事实会提醒同步。

orchestrator case 形如：

```cpp
case DispatchTarget::Foo_...: {
    using P = Pipeline<ghostlock::backend::FooPolicy,
                       ghostlock::terminal::RootChildPolicy>;
    static_assert(P::target == DispatchTarget::Foo_..., "dispatch case must match the pipeline's target");
    return P::run(exploit_session, document, debug_dir, force_attack);
}
```

---

## 二、新增 terminal

terminal 承载启动/交接，统一接口要求声明 `Input`（派生自 `contract::TerminalInput`）、
`activation`（`ActivationContext::Descendant` 或 `KernelSpawned`）与 `run(session, input)`。

| 位置 | 文件 | 做什么 |
|---|---|---|
| kind + identity | `contract/identity.hpp` | `TerminalKind` 取值、`namespace terminal { struct FooTerminal ...; }`、配对 `static_assert` |
| 可用性 | `contract/identity.hpp` | `terminal_available(Foo::kind)`；仅真机门禁后翻 true |
| 注册表 | `contract/identity.hpp` | `TerminalIdentityList` 加 `terminal::FooTerminal` |
| 输入词汇 | `contract/identity.hpp` | 中性基类 `TerminalInput`（`RootProgram` 等）；具体 `FooInput` 放 `terminal/` |
| 执行 policy | `terminal/foo.hpp`（+ `.cpp`） | `FooPolicy`：`kind`、`using Input = FooInput`、`activation`、`run` |
| 组合 | `pipeline/component_catalog.hpp` | 与某 backend 组成 triple；`terminal_name`/`terminal_from_token` |
| 分派 | `pipeline/orchestrator.hpp` | 新 `Pipeline<SomeBackend, FooPolicy>` case + target `static_assert` |
| 测试 | `backend_contract_test.cpp` | `TerminalIdentity`/`TerminalExecution`、Input/activation、注册表计数 |

要点：

- 中性词汇（`TerminalInput`、`ActivationContext`、`RootProgram`）在 `contract/identity.hpp`，
  以免出现 `contract -> terminal` 的 include 边；具体 `Input` 与 policy 在 `terminal/`。
- 选中的 backend 必须满足 `BackendExecution<Backend, FooPolicy::Input>`，否则 `Pipeline` static_assert 失败：
  一个 backend 只为它能填的输入类型满足执行契约。
- terminal 的职责边界不得合并：child 生命周期（`session/victim_*`）、root handoff/KernelSU 校验
  （`session/handoff_probe.*`）、UMH forward/wait（`terminal/umh_forward.*`，不得默认绑定 KernelSU）。

---

## 三、新增 route（backend 内部 middleware）

route 复用 profile 的 `RouteKind`，不是装配轴，不出现在 selection。新增 route 触点在 backend 内部。

| 位置 | 文件 | 做什么 |
|---|---|---|
| 注册 | `contract/model.hpp` | `RouteKind` 取值、`kRouteFoo`、`kRouteCatalog` 一行 |
| 可用性/命名 | `pipeline/component_catalog.hpp` | `middleware_available` 收录、`middleware_name` |
| Policy | `backend/cve_2026_43499/route/route_policy.hpp` | `FooPolicy : RoutePolicyDefaults`（编译期能力 + 需要的静态 hook）、追加到 `RoutePolicyList` |
| Route 本体 | `backend/cve_2026_43499/route/foo_route.{h,cpp}` | Route 类满足 `RouteLifecycle`（`prepare -> execute -> disarm -> destroy`，仅经 `status` 汇报） |
| 实例化 | `backend/cve_2026_43499/route/route_policy.hpp` / backend.cpp | 该 policy 的显式实例化（漏掉会链接失败） |
| 构建 | `src/Makefile`、`src/CMakeLists.txt` | 加入 `foo_route.cpp` |
| 传输 | `backend/cve_2026_43499/glkv3_schema.hpp`（v2: `schema.hpp`） | route 私有参数（如有）；两侧键名逐字一致 |
| Kotlin | `profile-core/.../data/route/RouteKind.kt` + `FooConfig.kt` | `(token, wire)` 与 `entries()/apply()/from()` |
| 导出 | `build.gradle.kts` | `routeFieldPaths` 加该 route |
| 测试 | `route_catalog_test.cpp`/`RouteCatalogAgreementTest.kt`、`route_policy_test.cpp`、`route_lifecycle_test.cpp` | canonical 列表、能力/派发计数、生命周期 |

要点：

- 只有该 route 才用的参数放 route 扩展节；共享代码会读的才进公共槽，两侧顺序必须一致。
- hook 定义在 Android 段，声明带 `[[gnu::noinline]]`，避免 LTO 把 route 实现内联进攻击函数。
- route 改动若触及攻击路径，必须以**真机门禁**验证（见 §五）；`cmp_disasm` 为可选诊断。

---

## 四、新增 platform / ancillary

- **platform ABI 字段**：`platform/abi.hpp` 加 owner Schema `FieldSpec` + GLKv3 `kPlatformAbiGlkv3Fields`
  + `apply_to` 的机械合并；同步 `platform_abi_test.cpp` / `glkv3_schema_test.cpp` 与 manifest。
- **platform 设备事实**：在 `platform/device_facts.hpp` 的 `DeviceProbeOps`/收集器扩展，保持 fail-closed；
  内核符号缺失记为事实而非致命。
- **ancillary 行为**：在 `ancillary/ancillary_policy.hpp` 实现 `AncillaryPolicyFor`；registry 与 gate
  由调用方注入（`ancillary/controller.hpp`），机制本身不固定行为。

---

## 五、构建、测试与门禁

新增组件至少补这些测试：

1. `backend_contract_test.cpp`：`BackendIdentity`/`BackendExecution<B, Input>`/`BackendState`、
   `TerminalIdentity`/`TerminalExecution`、注册表与 catalog 绑定。
2. `component_catalog_test.cpp`：`combination_supported`/`dispatch_target`/`dispatch_target_of` 与
   `selection_supported` 的一致性（新 triple）。
3. `route_catalog_test.cpp` / `RouteCatalogAgreementTest.kt`：canonical route 列表（新 route）。
4. `route_policy_test.cpp` / `route_lifecycle_test.cpp`：能力断言、派发计数与生命周期（新 route）。
5. `include_firewall_test.cpp`：新层边必须通过；白名单新增/移除要同步 count 断言。
6. `profile_manifest_test.cpp` / `profile_manifest_v3_test.cpp` + Kotlin
   `ProfileManifest{,V3}AgreementTest.kt`：wire 字段两侧一致（新字段/新 backend section）。
7. `fake_backend_test.cpp`：如需证明新的 backend/terminal 组合满足 R8 可组合性。
8. 新攻击路径函数加进 `tools/cmp_disasm.py` 的 `TARGETS`（当前 6 组）。

```sh
make -C src ghostlock            # 真机二进制
make -C src native-host-tests    # 主机单元测试（含 R1 防火墙与对拍）
make -C src lint-tidy            # clang-tidy（0 findings）
python3 tools/cmp_disasm.py build/native/ghostlock-B0 build/native/ghostlock
./gradlew :app:testDebugUnitTest --offline
```

最后在真机跑一次（冷机、固定 CPU 对、单 route、KernelSU 未加载），确认组件被选中、写验证通过，
并按 `docs/analysis/device-gates/` 格式归档。**未过真机不得把 `*_available` 翻 true、不得标 supported。**

---

## 核对清单

新增 backend：

- [ ] `BackendKind` + `backend::Foo` identity + `static_assert` + `BackendIdentityList`
- [ ] `backend_available`（默认 false，真机通过后再翻）
- [ ] `FooPolicy`（`kind`/`steps`/`state_from`/`run`，可选 State RAII）
- [ ] catalog triple + `DispatchTarget` + `backend_name`/`backend_from_token` + orchestrator case
- [ ] 构建、`backend_contract_test` / `component_catalog_test`、wire schema + manifest、Kotlin 枚举
- [ ] 真机门禁归档后才翻可用性

新增 terminal：

- [ ] `TerminalKind` + `terminal::FooTerminal` + `static_assert` + `TerminalIdentityList`
- [ ] `terminal_available`（默认 false）
- [ ] `FooPolicy`（Input/activation/run）+ 配对 backend 满足 `BackendExecution<B, Foo::Input>`
- [ ] catalog triple + `terminal_name`/`terminal_from_token` + orchestrator case
- [ ] `backend_contract_test`；真机门禁归档后才翻可用性

新增 route（backend 内部）：

- [ ] `RouteKind` + `kRouteCatalog`（两侧一致）
- [ ] `middleware_available` + `middleware_name`
- [ ] `FooPolicy`（能力 + hook）+ `RoutePolicyList` + 显式实例化
- [ ] Route 类（`prepare -> execute -> disarm -> destroy`）+ 入口 + Android hook 定义
- [ ] 构建/传输/HOCON/导出/Kotlin config + 测试矩阵 + `cmp_disasm`（触攻击路径时）+ 真机门禁

新增 platform / ancillary：

- [ ] owner Schema + GLKv3 FieldSpec + `apply_to` + manifest/测试
- [ ] ancillary policy（调用方注入 registry/gate/context）
