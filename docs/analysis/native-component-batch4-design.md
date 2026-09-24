# Native 组件架构 Batch 4 设计：frontend provider 接入（2026-09-23）

> 细化 `docs/analysis/native-component-architecture-plan.md` 的「Batch 4」。
> 依赖 Batch 3.1（`d008786`，wire→ComponentSelection 已接通）。**本批触及攻击关键路径**，
> `cmp_disasm` 与真机门禁是硬门槛。

## 现状与基线

- 分支 `very-not-stable-dev`；HEAD `d008786`；代码基线 `b411bb4`。
- frontend 选择：`main.cpp` 已从 wire 的 `component_ids.frontend` 构造 `ComponentSelection`
  （Batch 3.1）；`runtime::frontend_available` 当前只认 `RootChild`。
- 现有 “root_child frontend” 实现散落在：
  - `session/victim_context.hpp`：六个 pipe 端 + child pid；显式 `retire_child()` / `release_child()`
    转移，scope exit 不信号 child。
  - `session/victim_process.*`：`spawn_victim` 与 `verify_*` 回调（W1/W2/W3 校验）。
  - `session/handoff_probe.*`：KernelSU 模块/日志/enforce 轮询（`HandoffPollPolicy`）。
  - `route/exploit_procedure.*`：`ExploitProcedure::run` 固定 setup→W1→W2/W3→handoff，**同一条
    Template Method 同时承载 frontend（victim/handoff）、backend（W1-W3）与 middleware（route）**。
- `route/orchestrator.hpp`：校验 selection 后仍调用既有 `make_exploit_procedure`。

## 目标与范围

### 本批目标（总计划 Batch 4）

1. 把现有 root-child 启动与交接**归入 `root_child` frontend 边界**（仅移动职责，不改 W1/W2/W3 行为）。
2. 新增 UMH frontend 的独立 policy/config/adapter 文件；只定义与 Orchestrator/Session 的交互契约和
   故障回报，**不实现 UMH 执行**。
3. `app` 配置模型与 UI：允许选择/推荐 frontend，并展示 unavailable/unsupported 状态。
4. UMH 独立验证完成前不标 supported、不自动切换用户入口。

### 明确非目标

- 不实现 UMH 转发逻辑；不改 victim/child 协议、handoff 时序、W1/W2/W3。
- 不重写 `ExploitProcedure` 的 Template Method；不改 `ExploitSession` 字段布局。
- 不引入 PI 窗口内的间接调用、不新增可变全局。

## 硬约束

1. **`cmp_disasm` 8 攻击函数 IDENTICAL** 或差异经逐条复核。victim/handoff 的代码被内联进
   `do_one_write`/`run_main_route_threads` 等，任何实现改动都可能改变机器码。
2. **不改 `ExploitSession` 字段布局**（字段变化会移动 `waiter_thread` 栈偏移）。
3. frontend 选择在 PI 窗口外完成，保持编译期分派。

## 实现边界（用户确认，2026-09-23）

1. **D1=A 是契约脚手架，不是 frontend 已解耦**：契约只表达编译期 ID、能力/可用性与故障结果，
   由 Orchestrator 在攻击前使用；不加入当前无人调用的 provider 操作；**不把 Batch 4 标为 provider
   拆分完成**（`ExploitProcedure` 仍承载 child / W1–W3 / handoff）。
2. **拒绝分层**：未知 ID（不在 catalog）→ 解码阶段拒绝；已知但 unavailable（`umh_forward`、
   `cve_2026_64560`）→ 解码接受，由 Orchestrator 在攻击前以**明确错误**（报出 frontend/backend/
   middleware 三维）拒绝；host test 覆盖“已知不可用”与“未知”。无 UI、无可执行路径。
3. **职责区分**：`victim_context`/`victim_process` = child 生命周期边界；`handoff_probe` = root handoff /
   KernelSU manager 验证。二者不合并，**UMH frontend 不得默认绑定 KernelSU**；设计图与契约分列。
4. **D4 复跑**：以 Batch 4 的确切构建重新做真机门禁并归档；`cmp_disasm` 记录绑定**本批候选二进制**与
   基线标识，不引用 Batch 3 的旧记录。

## 现状 → 目标映射

| 计划概念 | native 现状 | Batch 4 动作（拟） |
|---|---|---|
| FrontendPolicy / Procedure（root_child） | victim/handoff + `ExploitProcedure::handoff` | 新增 frontend contract **声明**，root_child 指向现有实现；不搬代码（保机器码） |
| UMH frontend | 无 | 新增占位 policy/config/adapter：`available=false` + 交互契约与故障回报定义 |
| Orchestrator frontend 选择 | `selection_supported` 的 frontend 分量 | 明确用 frontend kind 校验；仅 root_child 通过，UMH 被拒绝并报错 |
| app frontend 选择/推荐 | 无 | 最小化；见 D3 |

## 待决方案（需用户拍板）

**D1 — frontend contract 的实现深度**

- **A（推荐，薄声明）**：新增 `frontend_contract.hpp`（编译期 policy：`kind`/`available`/故障回报类型）
  与 `root_child_frontend` 的**描述**，指向现有 victim/handoff；`ExploitProcedure` 不动。机器码不变。
- **B（实拆）**：把 `ExploitProcedure` 中 frontend 相关步骤抽成 frontend procedure，由 pipeline 组合。
  会改 `do_one_write`/`run_main_route_threads` 等机器码，需要重建反汇编基线 + 真机门禁。

建议 A：Batch 4 先建立边界与契约；实拆留到“生命周期/取消”专项（与 race 未闭环一起）。

**D2 — UMH 占位的深度**

- 建议：只声明 `umh_forward` policy 的 `available()==false`、所需 config schema 草案与故障回报契约，
  Orchestrator 对其返回 unsupported 并给出可诊断错误；不写任何执行路径。

**D3 — app/UI 范围**

- 建议：本批**不做 UI**（避免在未验证 UMH 时暴露选择）；仅在 DTO/模型层预留 frontend 字段与
  `available/unsupported` 语义。UI 选择/推荐推迟到 UMH 有真机证据之后（Batch 4.1 或 Batch 6）。

**D4 — 真机门禁**

- 沿用 Batch 3 条件：A301SO / `5.15.189-...`，KernelSU 未加载、固定 CPU 对、multicast、冷机；
  日志归档 `docs/analysis/device-gates/`。

## 数据流/控制流差异

```text
现状：main -> selection.frontend -> selection_supported -> make_exploit_procedure -> ExploitProcedure::run
目标：main -> selection.frontend -> Orchestrator 校验 frontend contract（仅 root_child）
        -> 既有 ExploitProcedure（root_child 语义不变）；UMH 被拒绝
```

不变量：8 攻击函数机器码不变；victim/child 与 handoff 的时序、所有权与清理顺序不变。

## 影响文件（拟）

| 文件 | 动作 |
|---|---|
| 新增 `src/core/route/frontend_contract.hpp`（或 `frontend/`） | root_child 声明 + UMH 占位 policy/契约 |
| `src/core/route/component_catalog.hpp` | frontend 可用性保持；补 UMH unsupported 的说明 |
| `src/core/route/orchestrator.hpp` | frontend 校验使用 contract；错误可诊断 |
| `src/core/session/*` | 仅注释：标注 root_child 边界与所有权（不改代码） |
| `src/core/tests/*` | frontend catalog/拒绝测试（root_child 通过；UMH 拒绝） |
| `docs/**` | 本设计 + 计划勾选 |

## 兼容性与回滚

- 不改 wire（Batch 2 已完成）；frontend 默认仍 `root_child`。
- 回滚单位：源码批次 + 反汇编基线。任一步非 IDENTICAL 且无法复核即退回。

## 验证矩阵

| 项 | 命令 | 预期 |
|---|---|---|
| host 单测 | `make -C src native-host-tests` | frontend 选择：root_child 通过、UMH 拒绝、未知拒绝 |
| 反汇编 | `python3 tools/cmp_disasm.py <baseline> build/native/ghostlock` | 8 函数 IDENTICAL 或已复核差异 |
| 构建/静态 | `make -B -C src ghostlock`、`make -C src lint-tidy` | 零告警、0 findings |
| 真机门禁 | 冷机、固定 CPU 对、multicast、KernelSU 未加载 | PASS 并归档 |
| Kotlin | `./gradlew :profile-core:test :app:testDebugUnitTest` | 保持通过（本批不改 Kotlin 行为，除非 D3 选择做模型预留） |

## 明确保留

- victim/child 协议、handoff 时序与所有权；`ExploitProcedure` 的 Template Method。
- `ExploitSession` 字段布局、`g_exploit_session` 唯一性。
- 不新增 PI 窗口内间接调用、不新增可变全局、不引入虚基类 provider。

## 进度

- [x] 只读调查 victim_context / victim_process / handoff_probe / exploit_procedure / orchestrator。
- [x] 产出本 Batch 4 设计；用户确认 D1=A / UMH unavailable / 无 UI / D4 复跑，并补 4 条实现边界。
- [x] 实现（薄声明 + 占位 + 解析分层 + 测试）：
  - 新增 `route/frontend_contract.hpp`（`root_child` available、`umh_forward` known-but-unavailable + 原因；不搬代码）；
  - 解析层改为“未知 ID 拒绝、已知 ID 接受”：`binary.cpp` / `NativeProfile.kt` 校验 `frontend/backend ∈ 已知集合`；
  - `main.cpp` 对不可用 selection 报出 frontend/backend/middleware 三维；
  - 测试：`profile_binary_test` / `ProfileRoundTripTest` 覆盖 UMH/64560 解码成功与未知拒绝。
- [x] 本地验证：`native-host-tests`、`make -B -C src ghostlock`（零告警）、`lint-tidy` 0 findings、
  `cmp_disasm`（Batch 3.1 候选 `5dcd8ddd…` → 本批候选 `fcbc2191…`：7 IDENTICAL + `do_one_write` 既有
  LAYOUT-SHIFT，RESULT PASS）、`./gradlew :profile-core:test :app:testDebugUnitTest` 通过。
- [ ] **真机门禁（本批构建重新跑）**：A301SO / 5.15、KernelSU 未加载、固定 CPU 对、multicast、冷机；
  归档 `docs/analysis/device-gates/`（绑定本批候选 `fcbc2191…`）。通过前 Batch 4 不算完成。
