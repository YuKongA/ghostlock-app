> 归档说明（2026-10-17）：归档原因 = **ADR 废除**（Lua / script / JCS 载体已删除，见 docs/plan/MASTER-PLAN.md §0.1）。原始路径 = docs/analysis/adr/0009-script-trust-and-hardline.md；归档路径 = docs/archive/20261017-1835-adr-0009-script-trust-and-hardline.md。

# ADR-0009：脚本信任模型与能力面硬线

> **已废除（2026-10-17 用户裁定）**：vendored Lua / mpack / nlohmann 与 GLKv3 文档路径已从构建和源码树删除，
> 本 ADR 的载体（Lua 沙箱、脚本面、文档解析）不再存在；保留此文仅为历史记录，现行边界见 `docs/plan/MASTER-PLAN.md` §0.1。

- 状态：Proposed（待用户批准，2026-10-07）
- 索引：[README.md](README.md)
- 日期：2026-10-07
- 相关：**ADR-0008**（Lua 运行期与沙箱）、ADR-0004（组合/分派）、ADR-0006（terminal 词汇）、
  `handoff-payload-plan.md` §10 D27/D29/D30/D31/D32、`handoff-plugin-lua.md`、`handoff-payload-kotlin.md` §5

## 背景（Context）

1. 用户需要导入**特殊优化的攻击路径** ⇒ **导入无限制**（任何 profile 均可携带 Lua 脚本，**不设确认/签名门禁**）。
2. 由此**安全责任转移**：从「工具拦截」改为「**能力面收窄 + 用户自检**」；错误代价可接受，**最坏是 panic**（需冷启）。
3. 硬线约束：**不得往磁盘乱写（尤其 AVB/分区）**——必须由**能力面**保证，而不是靠脚本自觉。

## 决策（Decision）

1. **三层信任模型（D27）**：**L1 声明层**（backend/op/route/params/顺序/条件）**自由导入**（不可执行）；**L2 脚本层**：可随 profile 导入，来源/作者/`sha256` 与「引用原语清单 + 能力需求 + 预算声明」**仅作展示信息，不拦截**；**L3 沙箱硬边界**（ADR-0008）。
2. **D29 硬线**：op 白名单内**不得存在块设备或任意路径文件写原语** ⇒ 脚本在**能力面**上无法触达 AVB/分区；**脚本可见面 = 7 条**（`backend`/`close`/`writeop`/`copyop`/`settle`/`handoff`/`cfg`；权威见 [lua-surface-design](../lua-surface-design.md) §1）；**③ 段 payload（收尾脚本 / LKM）由 Kotlin `resolve` 产出**、native 校验后落盘 ⇒ 唯一固定路径写（`GHOSTLOCK_HOME`，攻击前）。**沿革**：旧 `op` 词表已废。**新增任何含文件或块设备副作用的 op，必须列入「受限副作用清单」并单独评审。**
3. **D30 类型化句柄**：脚本对 native 资源只能持 `{kind, index, generation}` 句柄（有界 64），**不暴露裸指针/裸地址**；句柄由探针产生、使用按 kind 校验。
4. **D31/D32 插件控制面**：插件控制面采用 Lua，与 payload 共用**同一 VM、沙箱、预算与 manifest 校验**；插件**仍冻结**，**解冻时按 D31 形态**实现（不恢复旧 `.so` 单一通道）；`.so`/探针/`kernel_channel`/host 生命周期**保留给内核型插件**。
5. **分发侧**：恶意脚本由**用户自行严格检查**，不在本工具职责内（记为已知边界，不写成工具能力）。

## 裁定对齐（2026-10-08 · 用户裁定取代/修订）

- **用户裁定**：「**用户脚本正确性 = 用户责任**」⇒ **TD-RW2-1**（强制越权 + 技术债登记）。
- **放宽（不再做）**：**不为脚本逻辑错误做静态证明**（不证明顺序/取值在语义上正确）。
- **保留（不放宽）**：**窗口内零 Lua** · **逐调用边界校验** · **`plan_gate` 校验 staged 计划** · **预算**（指令/内存/单脚本 ≤64 KB）。
- **代价（如实）**：用户脚本改坏时**可能 panic**（本层不拦截）。
- **偿还时机**：**首个真机门禁（G2）后复评**（按真机证据决定是否加回部分静态检查）。
- 细则见 [lua-surface-design](../analysis/lua-surface-design.md) §3.1（`TD-RW2-1`）与 [rewrite-plan-v2](../../plan/rewrite-plan-v2.md) §4（`TD-RW2-*` 编号）。

## 残余风险与缓解（如实记录）

| 风险 | 缓解 |
|---|---|
| 脚本可编排既有原语 → 攻击失败 / dirty 终止 / panic | 原语**自带校验与 `verify`**、失败 fail-closed；脚本层失败与进程 dirty 语义分层；最坏 panic 需冷启（用户接受） |
| 导入内容不可信 | L2 只展示；**L3 沙箱**承担边界；D29 能力面无磁盘写 |
| 插件形态变化 | 解冻门禁（新沙箱/预算/描述符校验）与实现批次绑定，不随文档先行 |

## 后果（Consequences）

- 需新增 ADR 配套的**门禁判据**：能力面无块设备/任意路径写（用例断言 op 表）、预算隔离（单插件超限只影响自身）、句柄不可伪造、宿主 API 扩展须过受限副作用清单。
- 与 ADR-0008 配套生效；两者共同构成「脚本运行期 + 信任边界」的完整决策。

## 相关（References）

- **ADR-0008**；`handoff-payload-plan.md` §10 D27–D32；`handoff-plugin-lua.md` §3/§5。
