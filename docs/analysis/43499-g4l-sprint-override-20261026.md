# 43499 G4L 纯代码冲刺 · 知情强制沿革记录

- 日期：2026-10-26（America/Toronto）
- 类型：沿革 + 技术债登记（AGENTS.md §1「明确知情强制越权」唯一例外通道）
- 上位：`docs/plan/43499-approval-status.md` §4.2（批准状态）；`docs/plan/43499-resource-primitives-implementation-plan.md` §7.1

## 1. 用户指令

2026-10-26 用户指令：**「一次性做到 G4L-4M / G4L-4P / G4L-4T」**。Lead 就执行模式给出四选一（含真机门禁不可执行、G4L-0 仍 FAIL 的事实），用户明确选择 **B「纯代码冲刺」**：只实现 G4L-1..G4L-4T 代码，不做评审/门禁/测试，事后再补。此选择构成 AGENTS §1 所述「明确知情强制越权」。

## 2. 越过的条文与代价

| 越过的条文 | 内容 | 代价 |
|---|---|---|
| `AGENTS.md` §1/§2 | L 级改动先设计、再改动；阶段门 | 物理攻击代码未经 design-critic 阻断 0 即落盘 |
| `AGENTS.md` §3 | 套旧码 + TODO + 同批清零；机械改动需注明 | 冲刺批暂不执行，补验批补齐或按新文件说明 |
| `AGENTS.md` §5 | 核心攻击代码非作者资源审查 | 资源/顺序/UAF 审查延后 |
| `AGENTS.md` §7 | design-critic 常驻只读评审 | 延后 |
| `AGENTS.md` §8 | `git diff --check`、宿主测试、NDK 零告警、真机门禁 | 全部延后；真机门禁另因**本机无设备**（`adb devices` 为空）不可执行 |
| 指导书 §0/§2.3/§5 | 逐批用户批准、每批 SELF_ACCEPT、Lead 终验、只有 PASS 才进下一批 | 连续实施，不设中途 PASS；最终整体补验 |
| 实施计划 §5/§6 | 「上一批全绿才进下一批」、每批门禁、G4L-1 起真机冷机门 | 同批延后 |
| 实施计划 §7 | G4L-4T 独立批准点 | 由本次一次性指令覆盖（用户明示点名 4T） |

## 3. 当前基线（workspace 权威）

- `HEAD=9dd7e34c5849681857dc4274d4ab026ceef53df6`（`2.0.10`），用户在上一轮自行提交；包含 G4L-0 工作树、两份计划/指南、旧四件、Lead FAIL 记录、8 个 `app/.cxx` 构建产物。
- G4L-0 Lead 判定仍为 **FAIL**（`docs/analysis/43499-g4l0-lead-final-verification-20261026.md`）：冻结四件不覆盖最终树、plan §6 命令不可执行、5 个先存红测试。本冲刺不改变该判定，修复并入补验批。
- 索引仍有 15 个基线期幽灵条目（`AD`），`git diff --cached --check` exit 2；不归本冲刺修复。
- `adb devices` 无设备 ⇒ 真机门禁（冷机/固定 CPU 对/单 Primitive/KernelSU 未加载）不可执行。

## 4. 本指令的范围

**2026-10-26 后续指令**：用户再下“**冲刺到G4L-6**”，同一 B 纯代码冲刺模式扩展到 G4L-5、G4L-6 代码实施。

- **包含**：G4L-1 Placement、G4L-2 Landing/Page、G4L-3 Actor、G4L-4M Multicast、G4L-4P Pselect、G4L-4T TCP zerocopy 的 Native 物理实现（已完成）；G4L-5 Kotlin/Lua owner wrapper（Placement/Page/Actor `AutoCloseable` 类）；G4L-6 victim（Kotlin `ProcessBuilder`）、Finish handoff 脚本对象身份、ksud/marker 与 SELinux 恢复的验收代码（均只写代码）。
- **不包含**：G4L-6 真机最终验收（uid=0/ksud/marker/SELinux/资源终结）、默认 Flow/ksud 批接线、生产启用、profile/UI 文案/43284/64560 改动、提交/推送。
- 未补验前：九个入口即使已有物理实现，也**不得**标 `supported`、不得接默认 Flow、不得进入 ksud 批、不得宣称真机可用。

## 5. 技术债（偿还时机 = 补验批）

| 债 | 内容 | 偿还时机 |
|---|---|---|
| T-S1 | G4L-1..4T 全部：design-critic 复核、native-core owner/borrow/terminal 审查、宿主失败注入、Gradle 门禁、`git diff --check`、NDK 零告警、nm/readelf/APK 证据 | 代码完成后逐批补验 |
| T-S2 | 真机冷机门禁（G4L-1 起每批、4M/P/T 各自单 Primitive） | 用户提供真机后 |
| T-S3 | 四事务产物（DIFF/MODIFIED/VERIFICATION/ROLLBACK）按补验批重新生成并在独立副本回滚 | 补验批 |
| T-S4 | G4L-0 FAIL 三项阻断（B1/B2/B3） | 补验批第一项 |
| T-S5 | 5 个先存红测试（PayloadRequestsTest×3、ScriptRenderTest×2）与 plan §6 命令修正/T4 登记 | 补验批（用户已选「修正命令+另立修复批」方向） |
| T-S6 | 物理代码的 PI 窗口反汇编白名单、调用图证据 | 真机/补验批 |
| T-S7 | 若冲刺期间改动 C11 公共头（应避免）：补 ABI v3 布局冻结断言与评审 | 补验批 |
| T-S8 | G4L-5 wrapper：正常转移/clean 保留/dirty 消费/close 后拒绝/双 close/异常 finally 矩阵、R8 字段 retained、九入口对拍、Lua 反射契约文档 | 补验批 |
| T-S9 | G4L-6 真机验收：uid=0、脚本对象身份（SHA-256）、ksud/marker、SELinux 恢复、全部资源终结；真机不可用则保留 UNVERIFIED | 用户提供真机后 |
| T-S10 | G4L-6 handoff 脚本契约冻结复核与默认 Flow/ksud 接线批准（本冲刺明确不做） | 真机门 + 用户批准 |

## 6. 执行主体与边界

- 唯一写者：常驻 `coding-agent`；写范围由 Lead 逐批冻结（见共享任务板 task-3..task-11）。范围外全部冻结。
- Lead 只做记录、冻结、串行构建/合并审阅，不写攻击实现。
- 禁止：commit/push/stash/checkout/restore/reset/clean、CLI/环境变量/临时生产入口、改 `legacy*/general/43284/UI/Kotlin/profile/payload`。
- 本记录不构成本冲刺通过、supported 或生产可用；它只登记「用户知情强制 + 待偿技术债」。
