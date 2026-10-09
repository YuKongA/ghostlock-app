# 全流程 host 组装测试计划（三端打通 · R8）

> 分析类（≤8 KiB）。**自包含**：只引用本仓文件与规则编号。**边界**：本计划**不是真机判据**（每个用例的输出固定打印 `非真机判据`）；不覆盖内核行为 / PI 时序 / W1–W3 / LKM / handoff。
> 判据形式依据：[design-review-principles](../development/design-review-principles.md) **D1** + [handoff-payload-verification](handoff-payload-verification.md) §1（黑盒/白盒 + 极端输入值；不可能用例不测）。
> 沿革：取代 `docs/archive/20261007-2237-host-assembled-integration-test-plan.md`（v2）——保留「与真机同形」「extractor 纳入上游」，判据改写为现行形式并补三处实测缺口（§3）。

## 0. 目的

在**主机**上组装一条与真机同形的调用链，使三端（**Rust extractor → Kotlin 控制器层 → C++ native**）能在 CI 里逐段验证：控制器层把运行期文档编成 **长度前缀 GLKv3 帧**并**直接喂 native**（不经 `exportProfiles` 导出链），native 走真实的解析 → 契约归一 → dispatch 路径。

## 1. 装配链（与真机同形）

```
[profile 资产/HOCON] → [Kotlin 控制器层] → [运行期文档] → [长度前缀 GLKv3 帧] → [native 解析/契约归一/dispatch]
                                    ↑
                     [Rust extractor（上游：boot/OTA → schema 3 HOCON）]
```

- **同形性硬要求**：帧格式 = 真机同款（长度前缀 + GLKv3 + `schema == 3`）；控制器层调用 = 真机同款入口；native 侧**不得**为测试新增旁路（`I6`/`R13`：禁止新增 CLI/旁路）。
- **extractor 位置**：作为链条**上游**参与（其输出应进入同一条链），而不是旁支工具。

## 2. 判据（黑盒/白盒 + 极端值）

| # | 类型 | 输入（含**极端值**） | 观测点 | 预期 |
|---|---|---|---|---|
| C1 | 黑盒 | 空文档 / 只有 `schema`；超长键（=256 B）与 +1 B；缺失必填键 | native 进程退出码 + stderr 具名诊断 | 空文档 ⇒ 具名拒绝；=256 B 通过、+1 B 具名拒绝；缺键 ⇒ 具名拒绝（**不 panic**） |
| C2 | 黑盒 | 长度前缀 = 0 / = 实际−1 / = 实际+1 | native 读取行为 | 0 与截断 ⇒ 具名拒绝；+1 ⇒ 具名拒绝（不阻塞、不越读） |
| C3 | 黑盒 | 队列元素键数 = 12（=MAX）与 13（+1）；元素为非标量 | parse 结果 | =12 通过；13 ⇒ `param-invalid`（具名、`path=<section.key>`） |
| C4 | 白盒 | 同一份文档跑「控制器层 → 帧」与「native 解析」 | 两侧结构逐字段对拍 | 逐字段相等（**同一文档一处权威**） |
| C5 | 白盒 | extractor 输出（新机型语料） | extractor ↔ Kotlin ↔ native 三方产物对拍 | 逐字节/逐字段一致（**当前缺口 §3-③**） |
| C6 | 白盒 | probe 语料（`plugin-probe-conformance`） | Rust ↔ Kotlin ↔ native 三方判定一致 | 一致（**native 腿当前缺失 §3-①**） |
| C7 | 黑盒 | 运行完任意用例 | stdout 首行 | 固定打印 `非真机判据`（防止被当真机结论引用） |

## 3. 三端缺口与补齐（实测，逐条给 现状 · 目标 · 判据 · 门禁等级）

### ① probe 语料缺 native 腿

- **现状**：`core/src/tests/plugin_probe_test.cpp` 存在，但 `core/Makefile:441-450` 的构建规则**被注释**，不在 `native-host-tests` 聚合（`:218`）中；Rust 侧 `tools/extract_rs/tests/probe_conformance.rs` 与 Kotlin 侧 `app/src/test/resources/plugin-probe-conformance`、`plugin-probe-golden.tsv` 在跑，**native 缺席** ⇒ 三方判定无法对齐。
- **目标**：恢复 native 腿（受插件冻结影响的部分用**冻结期等价物**：直接喂固定语料 + 期望判定，不依赖插件运行时接线）。
- **判据（白盒 + 极端值）**：对同一语料，native 判定 == Rust == Kotlin；极端值 = 空描述符 / 字段缺一 / 哈希全 0 / 长度 = 上限与 +1。
- **门禁等级**：G1（宿主测试）；不触攻击路径 ⇒ **不需真机**。

### ② plugin-wire-shape-golden 的 native 校验被注释

- **现状**：`app/src/test/resources/plugin-wire-shape-golden.bin` 仍是冻结前的 golden，但 `core/Makefile:728-742` 的 native 校验规则**被注释**（注释原文：*USER DIRECTIVE 2026-10-05: plugin paused*）⇒ 字节形态**只有 Kotlin 侧**在看。
- **目标**：解冻批次恢复 native 校验；冻结期内以「native **拒绝**任何 `plugin.<id>` 段」为**契约断言**（这正是当前 fail-closed 行为）。
- **判据（黑盒/白盒）**：黑盒 = 含 `plugin.<id>` 段的帧 ⇒ native 具名拒绝；白盒 = golden 字节在解冻后逐字节被 native 接受且与 Kotlin 一致。极端值：段名大小写变体 / 空段 / 超出上限的 id 长度。
- **门禁等级**：G1（冻结期拒绝语义）+ 解冻批 G1+G2。

### ③ extractor 与另两端**无产物级对拍**

- **现状**：`tools/extract_rs/tests/**` 只覆盖**自身单元/语料**；全仓**未找到**消费 extractor 真实输出再与 Kotlin/native 产物对比的测试；现有跨端证据是**手写样本**（如 `app/src/test/kotlin/com/ghostlock/app/data/UserProfileStoreTest.kt:18/:34/:70` 一带）。
- **目标**：新增「extractor 输出 → Kotlin 解析 → native 解析」的**三方产物级对拍**（同一 HOCON 输入，三方各自产出规范形式后逐字段/逐字节比较）。
- **判据**：白盒 = 三方规范形式相等；黑盒 = native 对 extractor 产物**不报错**且 dispatch 命中与 Kotlin 预测一致；极端值 = 0 条 offset / 上限条数、uint64 边界值、空几何、重复键。
- **门禁等级**：G1（宿主）；若引入新机型资产 ⇒ 追加 G2（真机门禁，另行登记）。

## 4. 已覆盖段（现有对拍资产，避免重复造）

| 资产 | 覆盖的两端 | 位置 |
|---|---|---|
| profile-manifest-v3 / combination / vocabulary / stepset-steps / lkm-kmi（5 张 manifest） | native ↔ Kotlin（lkm-kmi 另有 Gradle 消费） | `app/src/test/resources/` + `profile-core/src/main/resources/`（双副本 + 对拍测试） |
| glkv3 golden-hex + fixture（91 行）+ native-doc-golden-v3.sha256 | native ↔ Kotlin | `make -C core glkv3-golden-hex` / `glkv3-native-fixture.tsv` |
| plugin-probe-golden.tsv + probe_conformance.rs | **Rust ↔ Kotlin**（native 腿缺，见 §3-①） | `app/src/test/resources/` + `tools/extract_rs/tests/` |
| plugin-wire-shape-golden.bin | Kotlin（native 校验缺，见 §3-②） | `app/src/test/resources/` |

⇒ **缺口清单**：① probe native 腿；② plugin-wire native 校验；③ **extractor ↔ Kotlin/native 产物级对拍**（三端里唯一完全没有的一环）。

## 5. 归属批次

| 批次 | 内容 | 门禁 |
|---|---|---|
| **B0.0（立计划）** | 本计划评审通过 + 装配链骨架用例（C1/C2/C7）落地 | G1 |
| **B1（落地）** | §3-③ extractor 三方对拍；§3-① probe native 腿恢复 | G1 |
| **解冻批（B3）** | §3-② plugin-wire native 校验恢复 | G1 + G2 |

## 6. 风险与代价（诚实交代）

- **不替代真机**：本链只能证「解析/归一/dispatch 与真机同形」，**不能**证内核行为与 PI 时序（§0 边界）；输出强制打印 `非真机判据`。
- **同形性漂移风险**：控制器层若为测试新增入口 ⇒ 违反 `I6`/`R13`；判据 C4 用「同文档两侧对拍」锁住。
- **冻结期妥协**：§3-①/② 的恢复受插件冻结约束，冻结期只做「拒绝语义」断言；解冻后**同批**恢复完整校验。

## 7. 未决

| # | 未决项 | 处置 |
|---|---|---|
| U-1 | extractor 侧「规范形式」由谁定义（字段序/数值编码） | B0.0 与 native/Kotlin 两侧对齐后落一页对照（**一处权威**） |
| U-2 | 跨端对拍是否需要真实机型语料（而非手写） | B1 用 2 个真实机型语料做样本，其余走手写极端值 |
| U-3 | 三方对拍的运行时长预算 | B0.0 实测后定；超预算则拆「快速子集 + 全量」两级 |
