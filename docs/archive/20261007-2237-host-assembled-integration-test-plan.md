# 归档头（docs/plan 批次，2026-10-07）

- 原始路径：docs/plan/host-assembled-integration-test-plan.md
- 归档原因：被 docs/plan/MASTER-PLAN.md 取代；未按现行设计规范编写
- 归档日期：2026-10-07 22:37（America/Toronto）
- 归档来源：task-63（docs-uml）

---

# 计划：Host 全量组装测试（v2 · 按真机调用链 · L 级 · 待用户认可）

> v2 取代 v1（同日）：v1 用 `exportProfiles` 导出链 ✗ —— 用户指出**真机是把文档直接传入 native**，
> 且 v1 未覆盖 extractor 等模块、调用点也不够深。v2 按**真机调用链**重写，并补 extractor 与控制器层入口。
> 级别：**L 级**（跨 Native↔Kotlin 契约 + 装配链）⇒ 先计划、获认可，再写码。
> 最高思想：`docs/development/软件工程守则.md`。日期：2026-10-07

## 0. 目的与边界
**目的**：在主机上组装**与真机同形**的调用链，验证**模块间信息传递**：
UI 控制器层（选择/合并/门禁）→ **运行期文档** →（**与真机同形地直接传给 native**）→ native 解析 → 契约归一 → dispatch 目标；
并把 **Rust extractor**（boot.img/OTA/URL → profile）纳入同一链条的上游。

**边界（不可越界宣称）**：
- 覆盖：**契约/装配层的信息传递**（含字节级同形性）。
- 不覆盖：内核行为 · PI-futex 时序 · W1/W2/W3 实际生效 · LKM 加载 · handoff · 不可逆段。
- **不是真机判据的替代**（守则 §4.2 / AGENTS「真机是功能的唯一判据」）；**不得**写入 `docs/analysis/device-gates/`；输出固定打印"非真机判据"。

## 1. 修正要点（相对 v1，逐条回应用户意见）
| # | 用户意见 | v1 的错误 | v2 的做法 |
|---|---|---|---|
| 1 | **真机是直接传入 native 的** | 用 `exportProfiles`（**导出链**）当输入 ✗ —— 与真机不是同一条链 | 取**运行期链**产物：控制器构造的运行期文档（落盘物 `profile.conf`/`profile.bin` 即**送入 native 的 GLKv3 字节**）⇒ 以**同一帧格式**（长度前缀文档）喂给 native |
| 2 | **extractor 等模块没覆盖** | 只覆盖 Kotlin↔native 一段 ✗ | 链条上游加 **extractor**：真实镜像/OTA 样本 ⇒ `--format conf` ⇒ 资产 ⇒ 控制器；并纳入 manifest 对拍链 |
| 3 | **调用点要深入 UI 控制器层** | 从数据层函数起测 ✗ | 测试入口 = **控制器公开入口**（选择 → 解析/合并/门禁 → 运行期文档），断言点在其下游各层 |

## 2. 目标装配链（与真机同形）
```
[extractor]  boot.img/OTA/URL ──(--format conf)──▶ profile 资产(HOCON, schema_version=3)
                                                        │
[控制器层]  用户选择(仅 available) ─▶ 解析/合并/声明门禁 ─▶ 运行期文档(NativeProfileDocument)
                                                        │  patchSafeMode → 落盘三件套(profile.conf/profile.bin)
[传入口]    **与真机同形**: 长度前缀 GLKv3 文档 ──(stdin 帧)──▶ native
                                                        │
[native]    帧解码 → 文档解析 → 契约归一 → dispatch 目标选择 → 归一化计划
                                                        │
[判据]      逐值断言 vs 权威 manifest；反例集必拒
```

## 3. 三段实现与"同形性"硬要求
1. **JVM 侧（控制器层入口，Robolectric 已具备）**
   - 入口：控制器公开入口（选择 → load/合并 → 运行期文档），**不绕过**门禁与归一。
   - 产出：**送入 native 的同一份字节**（运行期落盘的 `profile.bin`），并记录其身份（字节数 + sha256）。
2. **传入口（同形性核心）**
   - 必须使用**真机同款帧**（长度前缀文档；无密钥时不含会话帧）。
   - 严禁用 `exportProfiles` 产物代替 ✗（那是另一条装配链 ⇒ 会掩盖 P2 类缺陷）。
3. **native 侧（host）**
   - 用**生产函数**（帧解码 → 解析 → 归一 → dispatch）消费该字节；优先尝试**直接用生产入口** `--ghostlock-app-call` 的 host 构建。
   - ⚠️ 待核实（实施第一步）：`main.cpp` 是否 host 可编译（日志/Android 依赖）。若不可 ⇒ 调用同一生产函数并在报告里**明确标注差异**（"同代码路径、免 argv"），不得含糊。

## 4. 覆盖矩阵（v2）
- **A 正常链**：extractor 产出 → 资产 → 控制器 → 运行期字节 → native ⇒ 对每个 `available=true` 组合逐值断言
  （`cve_2026_43499 × {mcast,pselect,tcp}_{rootchild,shizuku}`、`cve_2026_43284 × umh`）。
- **B 反例（守卫必须能失败）**：token 列表形态 · 空队列 · 未声明 owner（`common`/`countermeasure`/`plugin`/`payload`）· `available=false` 被选 · **缺字面裸段的 backend 段**（今天必红 = 43499 现状）。
- **C 跨链一致性（新增，正是用户第 1 点暴露的缺陷类）**：**运行期链字节** vs **导出链字节**：允许不同，但**归一化后必须逐值同构**；
  若不一致 ⇒ 红（这条会把 P2「一事实两装配链」钉在门禁上）。
- **D extractor 段**：样本镜像 ⇒ `--format conf` ⇒ 资产 ⇒ 同一控制器链 ⇒ native；并核对 `schema_version=3`、必需项按声明路径。

## 5. 实施步骤（分批，单变量）
1. **M0 核实**：`--ghostlock-app-call` host 可编译性；控制器入口可测性；运行期字节落盘点。
2. **M1 A 链**：控制器 → 运行期字节 → native host 断言（先对 43284 绿、对 43499 红）。
3. **M2 B 反例集** + 证伪实验（造错 ⇒ 必红 ⇒ 撤回）。
4. **M3 C 跨链一致性**（运行期 vs 导出）。
5. **M4 D extractor 段**（真实样本 + conf 产出 + 入链）。
6. **M5 收口**：Makefile/KTS 目标、AGENTS 一行、本计划状态；临时物清零。

## 6. 判据与门禁
- host 绿 = **契约/装配层**判据 ✓；**真机门禁照旧不可替代** ✗。
- 期望值来源：权威 `combination-manifest.tsv`（单一权威）；运行期/导出两条链的**同构**由 C 组钉住。
- 建议顺序：**测试先行**（对 43499 现状必红）⇒ 再重落护栏补丁 ⇒ 转绿（先见证失败、再见证修复）。

## 7. 范围 / 风险 / 代价（诚实交代）
| 项 | 内容 |
|---|---|
| 新增 | 1 个 JVM 测试（控制器层入口）· 1 个 native host 测试 · Makefile 目标 · 1 个 KTS 编排任务 · 少量冻结向量 |
| 风险 | ① 被误当"免真机判据" ✗ ⇒ §0 边界 + 固定打印字样；② 传入口不同形 ✗ ⇒ §3.2 硬要求 + C 组；③ `main.cpp` 不可 host 编译 ⇒ 明确标注；④ 范围膨胀 ⇒ 按 M0–M5 分批，单变量 |
| 代价 | 比 v1 大（多 JVM/Robolectric 段与 extractor 段）；但**只有这样才能真正覆盖"信息传递"** |

## 8. 待认可（L 级门禁）
- [ ] 认可 v2（含 §0 边界与 §3.2 同形性硬要求）
- [ ] 认可分批顺序 M0→M5（可否先做 M0 核实，回报后再动码）
- [ ] 认可文件范围（新增 2 个测试 + Makefile/KTS 目标 + 计划/AGENTS 各一行）
