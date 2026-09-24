# Native 组件架构 Batch 3 设计：Orchestrator 与组件目录化（2026-09-23）

> 细化 `docs/analysis/native-component-architecture-plan.md` 的「Batch 3」。
> 依赖 Batch 1（`:profile-core`，`ad264c8`）与 Batch 2（v3 wire，`6dc288e`）。
> **本批触及攻击关键路径**，`cmp_disasm` 与真机门禁是硬门槛。

## 现状与基线

- 分支 `very-not-stable-dev`；HEAD `6dc288e`；代码基线 `b411bb4`。
- native 路由骨架（只读调查结论）：
  - `route/route_policy.hpp`：`RoutePolicy` concept + `RoutePolicyDefaults` + `SelectPolicy`/`TcpPolicy`/
    `MulticastPolicy` + `RoutePolicyList`；`make_route_policy`、`route_capability`、`run_route` 用
    `for_each_policy` 做**编译期直接分派**（无 vtable、无 `std::visit`）；`RouteKind` 来自
    `profile::RouteKind`。
  - `route/route_lifecycle.hpp`：`RouteLifecycle` concept（`prepare/execute/disarm/destroy/status`）+
    `run_route_lifecycle`。
  - `route/route_controller.{h,cpp}`：薄入口 `init(race, profile)` + `execute(request)`，调用
    `route::run_route(...)`。
  - `route/exploit_procedure.{hpp,cpp}`：`ExploitProcedure` 是 **Template Method 虚基类**（`run` 固定
    setup→W1→W2/W3→handoff；route 子类 override `resident_write`/`w2_fast_repair_*`/`w1_*_repair`/
    `w3_exact_target`）；`make_exploit_procedure(session, RouteKind)` 工厂。
  - `route/route_api.hpp`：`make_select_procedure`/`make_tcp_procedure`/`make_multicast_procedure` 与
    `do_*_fake_lock_route`。
  - `session/exploit_session.hpp`：`ExploitSession`（`runtime`/`profile`/`addresses`/`heap`/`race`/
    `victim`/`parked_victim*`）+ 进程唯一 `g_exploit_session`。
  - `main.cpp:67`：`make_exploit_procedure(session, decoded.route_kind())->run(decoded, dump_dir)`。
- `tools/cmp_disasm.py` 的 8 个攻击函数：`owner_thread`、`waiter_thread`、`consumer_thread`、
  `run_main_route_threads`、`do_kernel5_fake_lock_route`、`do_one_write`、
  `multicast_owner_worker`、`multicast_waiter_worker`。
- 现状结论：**middleware 的编译期 catalog 与静态分派已经存在**（`route_policy.hpp`）；Batch 3 主要
  缺口是命名/目录对齐到 frontend/backend/middleware、backend 能力边界、以及一个显式 Orchestrator
  入口，而不是重新发明分派。

## 目标与范围

### 本批目标（总计划 Batch 3）

1. 把 middleware（route）的目录与选择职责收敛到一个显式 Orchestrator；保持既有 policy/procedure
   生命周期与静态分派约束。
2. `session/**`：明确 frontend / backend / middleware 的公共上下文、错误结果与资源所有权；保留
   `g_exploit_session` 唯一可变 singleton。
3. 三种 middleware route 仅接入新配置 DTO 与目录，**不改内部算法/时序**。
4. CVE-2026-43499 既有 backend 仅经新 backend contract 暴露能力与状态；**不改漏洞原语实现**。
5. 测试：目录 ID、可用性检查、兼容组合拒绝、生命周期与资源清理。

### 明确非目标

- 不实现 UMH frontend / CVE-2026-64560 backend（Batch 4/5）。
- 不改 W1/W2/W3、route 算法、race 时序、payload 与内存布局。
- 不重写 `kernelsnitch/`、legacy v1、Shizuku/KSU 路径。
- 不引入 `std::function`/回调表/类型擦除；不新增可变全局。

## 硬约束（攻击路径）

1. **`cmp_disasm` 8 函数 IDENTICAL (strict)**，或差异经逐条反汇编复核并记录。任何改动若触及被内联进
   这些函数的头文件（`route_policy.hpp`、`route_lifecycle.hpp`、`exploit_procedure.*`、`session`），
   必须重新核对。
2. **不改变 `ExploitSession` 字段布局**：`RouteController` 的 `int32_t fallback_used` 曾因结构变窄而
   移动 `waiter_thread` 栈偏移（CPP17 实验）；本批不改 session/controller 的字段顺序与类型。
3. 编排层只在 **PI 竞争窗口之外**的边界引入；PI 窗口内仍是直接分派。
4. 不改 route/procedure 的内部步骤顺序与算术。

## 现状 → 目标映射

| 计划概念 | native 现状 | Batch 3 动作（拟） |
|---|---|---|
| Orchestrator | `main.cpp` 的 `make_exploit_procedure` + `route_policy` 分派 | 收敛为显式 `Orchestrator` 入口（非内联边界），语义不变 |
| middleware catalog | `route_policy.hpp` 的 `RoutePolicyList` + `profile::kRouteCatalog` | 命名对齐为 middleware ID；catalog 内容不变 |
| backend contract | CVE-2026-43499 散布在 `attack/ops`、`exploit_procedure` | 新增只读能力/状态接口，单实现占位 |
| frontend 边界 | `session/victim_process.*`、`victim_context.*`、`handoff_probe` | 归入 `root_child` frontend 边界（职责标注/薄接口） |
| session 上下文 | `ExploitSession` | 文档化各组件上下文与所有权；**字段不变** |

## 决策（已定，2026-09-23，用户确认「按推荐执行」）

- **D1 = 保留 `ExploitProcedure` 虚基类**：Batch 3 只让 Orchestrator 接管**选择**，`ExploitProcedure`
  作为共享 pipeline 被调用；去虚留到 Batch 4。
- **D2 = 单实现占位**：backend catalog 只登记 `cve_2026_43499`（`available()/state()`），frontend 只登记
  `root_child`；UMH/64560 留 Batch 4/5。
- **D3 = 设备可用**：A301SO（`5.15.189-...`，multicast）可用于真机门禁。
- **D4 = 接受不变量**：编排层在攻击路径外新增，8 攻击函数 IDENTICAL。

## 待决方案（已定，见上；保留供追溯）

**D1 — 是否本批消除 `ExploitProcedure` 虚基类**

- 总计划设计原则倾向"不用虚基类作为 route/provider 扩展机制"，但现状 procedure 是 Template Method
  虚基类。消除它会改变 `do_one_write`/`run_main_route_threads` 等函数的代码生成（`cmp_disasm` 可能
  不再是 IDENTICAL）。
- 建议：**保留虚基类**，把去虚留到 Batch 4（frontend 接入时统一评估）；Batch 3 只做目录/所有权与
  Orchestrator 入口收敛。

**D2 — backend/frontend catalog 本批的深度**

- 建议：**单实现占位**。backend catalog 只登记 `cve_2026_43499` 并暴露 `available()/state()`，不改变
  其执行；frontend 只登记 `root_child`，把 victim/handoff 归入其边界。UMH/64560 留 Batch 4/5。

**D3 — 真机门禁设备**

- 历史可用设备是 A301SO（`5.15.189-...`，multicast）。本批若触攻击路径，需按 AGENTS 跑冷机、固定
  CPU 对、单 route、KernelSU 未加载的门禁；设备不可用时本批**保持未完成**，不以 host 测试替代。

**D4 — 不变量口径**

- 接受"编排层在攻击路径外新增、8 攻击函数 IDENTICAL"作为本批不变量。若实现中无法保持 IDENTICAL，
  先停下复核差异，不强行合入。

## 影响文件（拟）

| 文件 | 动作 |
|---|---|
| `src/core/route/route_policy.hpp` | 命名/注释对齐 middleware；**逻辑不变** |
| `src/core/route/route_controller.{h,cpp}` | 经 Orchestrator 入口调用；字段不变 |
| `src/core/route/exploit_procedure.{hpp,cpp}` | 保留虚基类；仅注释/所有权说明（D1） |
| `src/core/route/route_api.hpp` | 工厂签名不变；如新增 Orchestrator 声明 |
| `src/core/session/exploit_session.hpp` | 文档化 frontend/backend/middleware 所有权；字段不变 |
| 新增 `src/core/route/orchestrator.*` / `backend_contract.*` / `frontend_contract.*` | 编译期 catalog + 薄接口（非 PI 窗口） |
| `src/core/main.cpp` | 经 Orchestrator 选择 procedure |
| `src/core/tests/route_catalog_test.cpp` 等 | 目录 ID、可用性、组合拒绝、生命周期 |

## 数据流/控制流差异

```text
现状：main -> make_exploit_procedure(session, route) -> ExploitProcedure::run
        race threads -> RouteController::execute -> route::run_route(policy) -> do_*_fake_lock

目标：main -> Orchestrator::run(session, selection) -> Frontend(root_child) + Backend(cve_2026_43499)
        + Middleware(tcp|select|multicast) -> 既有 procedure/lifecycle
       （编排在 PI 窗口外；窗口内仍 for_each_policy 直接分派）
```

不变量：8 攻击函数机器码不变；route 生命周期顺序（prepare→execute→disarm→destroy）不变；资源所有权
与清理边界不变。

## 兼容性与回滚

- 本批不改 wire（Batch 2 已完成）；`profile::RouteKind` 与 middleware ID 的映射保持 1:1。
- 回滚单位：源码批次 + 反汇编基线（`build/native/ghostlock` 的 8 函数）。任一步 `cmp_disasm` 非
  IDENTICAL 且无法复核，退回。
- 真机门禁记录按 `docs/analysis/device-gates/*.md` 模板归档。

## 验证矩阵

| 项 | 命令 | 预期 |
|---|---|---|
| host 单测 | `make -C src native-host-tests` | 目录 ID、可用性、组合拒绝、生命周期通过 |
| 反汇编 | `python3 tools/cmp_disasm.py <baseline> build/native/ghostlock` | 8 函数 IDENTICAL (strict) 或已复核差异 |
| 构建 | `make -B -C src ghostlock`、`make -C src lint-tidy` | 零告警、0 findings |
| 真机门禁 | 冷机、固定 CPU 对、单 route、KernelSU 未加载 | route 命中与写验证通过；日志归档 |
| Kotlin 回归 | `./gradlew :app:testDebugUnitTest :profile-core:test` | 不受影响，保持通过 |

## 明确保留

- `kernelsnitch/`、legacy v1、现有 CVE-2026-43499、W1/W2/W3、三种 middleware 的算法与时序。
- `ExploitSession` 字段布局、`g_exploit_session` 唯一性、`route_lifecycle` 的 concept 与顺序。
- 不新增 PI 窗口内的间接调用、不新增可变全局、不引入虚基类 provider（D1）。

## 进度

- [x] 只读调查 `route/**`、`session/**`、`main.cpp`、`cmp_disasm.py` 与 catalog 测试。
- [x] 产出本 Batch 3 设计；D1 保留虚基类 / D2 占位 / D3 设备可用 / D4 接受（用户确认）。
- [x] 实现（header-only，不进入攻击 TU）：
  - 新增 `route/component_catalog.hpp`（frontend/backend/middleware id、availability、
    `selection_supported`、诊断名）与 `route/orchestrator.hpp`（校验后交给共享 `ExploitProcedure`）。
  - `main.cpp` 经 `NativeOrchestrator` 选择；`route_policy.hpp` 注释标明为 middleware catalog；
    `session/exploit_session.hpp` 文档化 frontend/backend/middleware 所有权（字段不变）。
  - 新增 `tests/component_catalog_test.cpp` + Makefile 规则。
- [x] 本地验证：
  - `make -C src native-host-tests` 全通过（含 `component_catalog_test`）；
  - `make -B -C src ghostlock` 零告警；
  - `NDK_ROOT=... make -C src lint-tidy` exit 0、0 findings；
  - `cmp_disasm` vs Batch 3 前基线：7 IDENTICAL + `do_one_write` 既有 LAYOUT-SHIFT，RESULT PASS
    （无恶化）。
- [~] **范围修正（Batch 3 审查后）**：本批仅落地 catalog 与薄 Orchestrator 骨架；`route_controller.*`/
  `exploit_procedure.*` 未改，未消费 wire 的 frontend/backend 选择，backend contract、可审计 Session
  所有权追踪、生命周期/清理测试未做（转 Batch 3.1，并入 Batch 1/2 review F4）。
- [x] **真机门禁：PASS**（`docs/analysis/device-gates/B3-20260923-multicast-direct-pass.md`）：
  A301SO / `5.15.189-...-ab14546557`，KernelSU 未加载、固定 CPU 对 3/4、multicast 单组合、direct；
  多次 `route_done success=1`、`child is root`、无 panic。早期非确定 panic 归为 `KERNEL-PANIC-01`
  （附 pstore Oops 证据）。

### 实现偏差

- 采用 header-only orchestrator（不新增 `.cpp`），使编排层完全留在攻击 TU 之外，保证 8 函数不变；
  后续批次需要独立编译单元时再拆分。
- `MiddlewareKind` 直接复用 `profile::RouteKind`（避免两套 route 枚举漂移）；`Auto` 不可选。
