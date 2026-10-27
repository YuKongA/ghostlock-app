# 43499 G4L-1～G4L-6 结构阻断修复记录

- 日期：2026-10-26（America/Toronto）
- 触发：用户终验不通过（[43499-g4l-final-review-20261026.md](43499-g4l-final-review-20261026.md)），指令「先修结构阻断，再补真机门禁」
- 模式：B 纯代码冲刺延续（只写代码 + 编译烟雾；评审/门禁/测试/真机验收仍未做）
- 基线：`HEAD=9dd7e34c5849681857dc4274d4ab026ceef53df6`（未 commit / 未 push）
- **状态：结构阻断 F-A～F-D 代码修复完成（10 项用户阻断全覆盖），编译绿；待独立复核 + 真机门禁。仍不得标 PASS/supported/生产可用。**

## 1. 用户 10 项阻断 → 修复映射

| # | 用户阻断 | 修复（Lead 独立 grep/read 复核证据） | 主要文件 |
|---:|---|---|---|
| 1 | PI 窗口 `waiter_trigger` 函数指针间接调用 | 删除 `carrier_trigger_fn`/`waiter_trigger`/`bind_trigger`（grep=0）；新增 3 个静态 waiter 入口，prepare 阶段 switch 选 pthread 入口，窗口内直接调对应 `carrier_<kind>_trigger` | actor.c、carrier_internal.h |
| 2 | dirty 报 `pageConsumed=1` 但未持有 Page | dirty 回滚前先 `page_from_handle()->state=CONSUMED`、`actor->page=Page`、`retained=1`，再写 `page_consumed=1`；clean 回滚仍=0 归还 Kotlin | actor.c |
| 3 | PSELECT 覆盖/关闭 JVM fd、stdio 不可恢复、double-close | 目标 fd 全集（含 0/1/2 与任意 JVM fd、nfds-1）`F_DUPFD_CLOEXEC` 备份表（frame 尾部，零新增全局）；安装失败回滚；release/stuck 先幂等恢复备份再关自有 src；backup/owned 不相交、double-close=0 | carrier_pselect.c |
| 4 | JNI 逐字段写半数→Kotlin 误恢复已消费 token | JNI 前置校验（NOT_ACCEPTED 不调 core）；子类字段先写、`callStatus` 最后写；Set* 失败清异常返回 INCOMPLETE（int `INT32_MIN`/handle `-1`）；Kotlin 三态结算：NOT_ACCEPTED 恢复、ACCEPTED 按 receipt、INCOMPLETE fail-stop 不恢复/不重试/不 close | jni43499.c、Native43499Jni.kt、owned/*.kt |
| 5 | Placement/Page 清理忽略失败→假 released | 清理助手全部带 `clean` 出参并逐项记账；仅全成功才 `owned_chain_released=1` 并抹身份；任一失败 ⇒ DIRTY/QUARANTINED retained、身份保留不 free；`closeActor` 接入 `page_release_checked`，Page 释放失败⇒retained | placement.c/.h、page.c/.h、actor.c |
| 6 | Carrier 物理结果未参与成功判断 | 新增 `glk43499_carrier_outcome{operations,calls,successes,failed,observed_write}`；executeActor 仅 `observed_write && calls>0 && successes>0` 才 CLEAN_SUCCESS，否则 INTERNAL_ERROR/CLEAN_FAILURE | actor.c、carrier_internal.h、carrier_*.c |
| 7 | TCP 地址边界加法溢出 | `glk43499_internal_add_u64`/`add_uintptr`（`__builtin_add_overflow`）；TCP mapping_base+offset、offset+page 与 pselect 尺寸乘加、placement 数量式全部审计 | carrier_tcp.c、core_internal.h、carrier_pselect.c、placement.c |
| 8 | Finish 无执行链；ksud 仅文件存在；marker 未绑定本次运行 | NEW `FinishExecution`（uid==0 直跑 `/system/bin/sh`，否则 `su -c` seam；有界 kill、exit/输出摘要/marker 内容）；脚本注入 `GHOSTLOCK_RUN_ID`，marker 内容绑定 runId 并双通道校验；ksud 改为有界 `--version` 命令证据（exit==0+摘要），否则 `ksud-unproven` | ui/FinishExecution.kt、FinishScript.kt、FinishHandoffAdapter.kt、finish/FinishExecutionEvidence.kt、FinishAcceptance.kt |
| 9 | victim PID/task 与 Actor 目标无可审计绑定 | victim 证据 `{pid, /proc/<pid>/stat starttime, uid}`；run 把 victim 身份与 PrimitiveSpec 三地址绑定为 `RunBindingEvidence`，execute 后复查 starttime 一致；验收校验绑定/uid/地址 | victim/*.kt、run/Cve43499Run.kt、finish/FinishExecutionEvidence.kt、FinishAcceptance.kt |
| 10 | 计划/完成记录/实际文件范围矛盾 | 本记录 + 完成记录头部「冲刺快照」声明 + 批准表状态更新；文件清单以工作树与本记录为准 | docs（Lead） |

## 2. Lead 独立验证（结构修复后）

- `./gradlew :app:externalNativeBuildDebug :app:compileDebugKotlin --console=plain` → **BUILD SUCCESSFUL in 725ms，12 tasks（5 executed），EXIT=0**。
- 静态复核：`waiter_trigger|carrier_trigger_fn|bind_trigger`=0；`page_release_checked` 已接入 closeActor/page close；pselect 备份/恢复函数存在；`callStatus` 最后写函数存在；carrier outcome 成功判据存在；overflow helper 已用于 TCP；`FinishExecution`/`FinishExecutionEvidence`/`GHOSTLOCK_RUN_ID`/`RunBindingEvidence` 存在。
- 范围：公共 C11 头 ABI 布局零改动；未接默认 Flow/UI；后端零 `import ui`；无新增可写全局；未改 docs（本轮修复期）；未 commit。

## 3. 已知残余 / 未验证（不得据此称 PASS）

1. 真机冷机门禁、单 Primitive 命中/写验证、PI 窗口「零 BLR」反汇编证明、fd 备份/恢复实测、marker runId 与 ksud 命令实机、`/proc/<pid>/stat` 可读性与 tick 解析、victim starttime 复查、超时 kill、失败注入矩阵——全部 UNVERIFIED（本机无设备）。
2. `kernelsnitch_cleanup` 无 errno 返回 ⇒ 其 munmap 无法记账（placement.c 注释已写明；glgeneral 只读）。
3. pselect 把「原本未打开」的目标 fd 也纳入恢复逻辑（恢复=关闭安装的 dup），真机需确认目标集合形状。
4. `FinishExecution` root 判定 `Process.myUid()==0`；非 root 走 su seam，真机需验证授权与 marker 权限。
5. Gradle 全门禁（`assembleRelease` / `git diff --check` / `nm/readelf/APK/DSO SHA`）与既有 5 个先存红测试仍未复跑；`FinishScript` marker 段变更可能改变 `ScriptRenderTest` 期望，补验批需同步。
6. 结构修复本身仍未经 design-critic/native-core 复核。

## 4. 口径

- 当前：**实验代码、结构修复完成、待独立复核与真机门禁**；不得标 `supported`、不得接默认 Flow/ksud、不得宣称 uid=0 或生产可用。
- 后续：task-9 补验批（结构修复复核 + 全门禁 + 真机门）与默认 Flow/ksud 接线批准仍是独立门。
