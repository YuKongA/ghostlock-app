# Batch 5 计划：backend 扩展点与 CVE-2026-64560 占位接入（2026-09-24）

> 依据整体计划「Batch 5：backend 扩展点及第二后端接入」。**CVE-2026-64560 无实现**，本批只落
> backend contract 与占位 schema/拒绝链；不得猜测其设备偏移字段（整体计划 L338）。本批不触
> 攻击关键路径（64560 无可执行路径）。

## 现状

- backend 声明：`route/component_catalog.hpp`（`BackendKind::{Cve2026_43499,Cve2026_64560}` +
  `backend_available`）、`route/backend_policy.hpp`（两个声明型 struct + `static_assert` 绑定 catalog）。
- 执行：`session/backend/cve_2026_43499_backend.*`（`Cve2026_43499Policy` 模板化）；
  `orchestrator` 的 `combination_supported()` 只认 43499 → 64560 在攻击前被 `Rejected`。
- wire/解码：v3 精确校验 backend ∈ 已知集合（64560 解码**接受**、派发拒绝）。
- Kotlin：`profile-core` 的 `ComponentKind.kt` 镜像 wire 值/可用性（D3 预留）。
- 64560：**无模块、无 schema 字段、无执行路径**。

## 目标（本批）

1. **backend contract**：编译期 `BackendPolicy` concept + 注册表，明确 backend 必须提供的接口
   （`kind` / `available`，可用时 `run<Middleware>`），`Cve2026_43499Policy` 满足；
   `Cve2026_64560Policy` 以 `available=false` 占位满足（无 `run`）。
2. **CVE-2026-64560 模块占位**：`session/backend/cve_2026_64560_backend.{hpp,cpp}`，只声明 id /
   可用性 / 拒绝原因；**不实现原语、不定义设备字段**。
3. **profile schema 占位与隔离**：64560 拥有独立 section 标识（wire 已按 backend id 分区）；
   解码只校验 id/版本，字段集为空；测试断言 43499 字段不会进入 64560 section。
4. **测试**：contract `static_assert`、64560 解码接受 + `combination_supported`/`Rejected`、
   schema 隔离（host + Kotlin 如涉及）。
5. **Kotlin 占位**：backend 类型/校验与 native 一致性（沿用 D3，不做 UI、不标 supported）。

## 非目标

- 不实现 CVE-2026-64560 漏洞、不猜设备偏移、不做 UI、不新增真机 gate（不可用 backend 无执行路径）。
- 不改 43499 的算法/字段；不复用其字段作为 64560 的默认值。

## 影响文件（拟）

| 文件 | 动作 |
|---|---|
| `src/core/route/backend_policy.hpp` | 新增 `BackendPolicy` concept + 注册表（可用性权威仍在 component_catalog） |
| `src/core/session/backend/cve_2026_64560_backend.{hpp,cpp}` | 占位 policy（available=false + 拒绝原因） |
| `src/core/route/component_catalog.hpp` | 不变（组合表仍只认 43499） |
| `src/core/tests/*` | contract / 拒绝 / 隔离测试 |
| `app/**/ComponentKind.kt`（如需要） | 占位校验一致性 |
| `docs/**` | 本计划 + 整体计划同步 |

## 验证

| 项 | 命令 | 预期 |
|---|---|---|
| host | `make -C src native-host-tests` | 通过（contract / 拒绝 / 隔离） |
| 构建/静态 | `make -B -C src ghostlock`、`make -C src lint-tidy` | 零告警、0 findings |
| 反汇编 | `python3 tools/cmp_disasm.py <prev> build/native/ghostlock` | 不触 8 函数（预期 PASS） |
| Kotlin | `./gradlew :profile-core:test :app:testDebugUnitTest` | 通过 |
| 真机 | — | 不可用 backend 无执行路径，不 gate |

## 开放问题（待确认）

1. 64560 的 section / schema 版本是否现在确定一个**空 schema 版本号**，还是等漏洞实现时再定？
2. Kotlin 侧本批是否加占位类型（与 D3 一致），还是只做 native？
3. backend contract concept 的粒度：只校验 `kind` / `available`，还是同时校验可用 policy 的
   `run<M>` 返回类型？

## 进度

- [x] 现状只读梳理；产出本计划（2026-09-24）。
- [ ] 用户确认目标与上述开放问题。
- [ ] B5-1 contract、B5-2 占位模块、B5-3 隔离测试、B5-4 Kotlin/文档。
