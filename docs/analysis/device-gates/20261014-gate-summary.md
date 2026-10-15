# 门禁汇总（J7）· 2026-10-14

集中入口（用户要求：所有测试通过 gradle 集中控制，可通过 gradle 控制 cmake 测试）：

| 命令 | 覆盖 | 现状 |
|---|---|---|
| `./gradlew checkAll` | 宿主：原生 CTest（`nativeTestHost`）+ app 单测（`:app:testDebugUnitTest`，内含 native 协议表对拍） | **绿**：128/128 CTest，app 26 例 0 失败 |
| `./gradlew deviceGate` | 真机：装 APK → 起临时界面 → 抓 logcat → 按 native 导出的 manifest **机器校验** 7 项 | **绿**：7/7 命中，日志 `build/app/device-gate.log` |
| `./gradlew gateAll` | 上面两条一起 | 绿 |
| `./gradlew markdownlint` | 文档渲染级问题（不假绿：跑不动显式 SKIP） | 绿 |

## deviceGate 校验的 7 项（值全部来自 native 导出的 manifest，脚本不手抄）

1. `gate: end` —— 门禁整段跑完（中途崩掉不会出现）
2. `sha256=<splice_helper>` —— 1440B 的 helper 图在真机上逐位一致
3. `sha256=<landing_payload>` —— 480B 的落地载荷逐位一致
4. `branch=<branch_word>` —— sentry 计划的分支字
5. `displaced=<displaced_word>` —— 被替换的指令词
6. `plan: cve_2026_43499 step=w1 outcome=Refused` —— 计划按 backend 走到第一步并具名拒绝
7. `plan: cve_2026_43284 step=patch1 outcome=Refused` —— 同上

## 四道门各自证明什么（互不替代）

| 门 | 证明 | 不证明 |
|---|---|---|
| 宿主单测 | 边界/协议/op 层的**规则**（含证伪实验：尾随字节、句柄代际、形状极性、宽度） | 任何设备事实 |
| host+android 构建 0 warning | 两个工具链下都能编译干净 | 行为正确 |
| app 单测 | Kotlin 侧契约、协议表与 native 一致、步骤引擎四条规则、日志规则 | 设备行为 |
| 真机 deviceGate | 库能加载、会话能建、Snapshot 真实现、未接线 op **具名拒绝**、图与钩子数据原样穿过 JNI、计划在真机上按顺序停下 | 任何写原语（设备 seam 属 J3b/J4b） |

## 已归档的真机证据

| 文档 | 内容 |
|---|---|
| `20261014-jni-boundary-gate.md` | 三次门禁：边界与拒绝面；43284 数据 op（两端摘要一致）；换日志系统之后 |
| `20261014-kotlin-steps-gate.md` | 步骤引擎在真机上按 backend 走一步并具名拒绝后停下 |