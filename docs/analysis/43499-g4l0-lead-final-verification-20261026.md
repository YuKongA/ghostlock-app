# 43499 G4L-0 Lead 终验记录

- 日期：2026-10-26（America/Toronto）
- 级别：L
- 冻结快照：`HEAD=d9b180310ae35cf697843c64c7f2b26756036ef4`（branch `dev-nocpp`）+ 工作树 G4L-0 修订
- 批准范围：仅 G4L-0（线性契约修订）。G4L-1 及以后未批准。
- 判据：`docs/plan/43499-agent-coding-and-self-acceptance-guide.md` §5、§2.3；`AGENTS.md` §7/§8
- **终验判定：FAIL**（不是 BLOCKED-BY-EVIDENCE：证据已取得且显示门禁与交付证据不闭合）

## 1. 身份与范围（Lead 独立复核）

有效工作树 = HEAD + 以下 14 条批次路径（13 tracked + 1 新增权威计划）+ 新增指南（审核依据）。
工作树内 `app/.cxx/**` 8 个被跟踪构建产物为 HEAD 基线期即已存在、门禁重跑继续写入的构建噪声，不属交付 diff。

| 文件 | sha256（终验） |
|---|---|
| core/abi.h | `6e67615d…` |
| core/result.h | `8b77c7ed…` |
| core/placement.h | `75d01206…` |
| core/page.h | `cca1f8dd…` |
| core/actor.h | `24d9189a…` |
| core/api_unavailable.c | `2e2df015…` |
| jni43499.c | `bbf10491…` |
| Native43499Jni.kt | `3b3428d0…` |
| full-process-uml.md | `6a50a16b…` |
| 43499-43284-jni-rewrite-plan.md | `d304180f…` |
| 43499-approval-status.md | `b683f00f…` |
| 43499-c11-header-contract-plan.md | `918f0460…` |
| 43499-jni-transport-plan.md | `8eecd17c…` |
| 43499-resource-primitives-implementation-plan.md | `e636dd94…`（11658 B） |
| 43499-agent-coding-and-self-acceptance-guide.md | `e472050b…`（16010 B） |

身份结论：
- `git archive HEAD` + `patch DIFF_FILE(e4b88c00…)` ⇒ 13 条 tracked 路径与工作树逐字节 SAME；唯一差异=新增计划文件。
- 计划自 DIFF 快照后有 2 行指南指针增补（11446→11658 B，sha `0800c8ae…`→`e636dd94…`）；指南（16:30）、计划指针（16:30）均晚于四件（16:25–16:26）。见 B1。
- 评审窗口内 15 个目标文件 hash 两次一致（16:50Z/16:52Z）；无源文件 writer。
- `git status --porcelain` 桶：`??` 4（2 计划 + 2 旧批 artifacts 目录）、`AD` 15（HEAD 基线期即存在的幽灵索引条目）、`M` 21（13 批次 + 8 `app/.cxx` 构建产物）。见 B3。

## 2. 独立门禁重跑（Lead 串行执行，literal 证据）

环境：Gradle 9.8.1 / AGP 9.4.1 / NDK 30.0.16248370 / java 25.0.3。

| # | command | literal result | exit |
|---|---|---|---|
| 1 | `git diff --check` | 空输出 | 0 |
| 2 | `./gradlew nativeTestHost nativeBuildAndroid :app:testDebugUnitTest --console=plain` | `Task 'nativeTestHost' not found in root project 'GhostLock' and its subprojects.` · BUILD FAILED in 426ms | 1 |
| 3 | `./gradlew :app:compileDebugKotlin :app:externalNativeBuildDebug :app:assembleRelease --no-daemon` | BUILD SUCCESSFUL in 50s；65 actionable tasks: 20 executed, 45 up-to-date；warning: 0 行 | 0 |
| 4 | `./gradlew :app:testDebugUnitTest --console=plain` | 51 tests completed, 5 failed；BUILD FAILED in 6s | 1 |
| 5 | `./gradlew :app:externalNativeBuildDebug --console=plain` | BUILD SUCCESSFUL in 713ms | 0 |
| 6 | HEAD 干净副本：`./gradlew :app:testDebugUnitTest --console=plain` | 51 tests completed, 5 failed（同一组 5 例） | 1 |

门禁结论：
- 门禁 2 **不可执行**：`nativeTestHost`/`nativeBuildAndroid` 未在 `build.gradle.kts`/`app/build.gradle.kts` 注册（`core/` 已删）；`introduction.md` §二「现行命令」映射已失真。属 B2。
- 门禁 4 **红**：失败=P`ayloadRequestsTest`(3)、`ScriptRenderTest`(2)。干净 HEAD 副本复现同一 51 例/5 失败 ⇒ **预先存在、非 G4L-0 回归**；但 plan §6 未登记任何例外，按 guide §5.6/§2.3 门禁未绿。属 B2。
- 门禁 3/5 绿；`assembleRelease` 产出 APK（见 §3）。

## 3. 契约 / 静态 / 产物核验（Lead 独立执行）

- **ABI v3 布局**：HEAD 头文件 vs 当前头文件，clang 实测 20 个 struct `sizeof` + 全部字段 `offsetof` 输出 `diff` 完全一致；4 个改名字段 offset 与 HEAD 同位置。`GLK43499_ABI_VERSION=3`，枚举数值未变。PASS。
- **头文件自包含**：7 头 × C11 + C++23，`-Wall -Wextra -Wconversion -Wsign-conversion -Werror -fsyntax-only` ⇒ 14/14 exit 0，零告警。PASS。
- **C 源严格语法**：NDK clang `aarch64-linux-android31`，`-Wconversion -Wsign-conversion -Werror -fsyntax-only`：`jni43499.c`、`api_unavailable.c` 均 exit 0、stderr 0 字节。PASS。
- **符号**：`libghostlock43499.so` 动态导出恰 9 个 `Java_com_ghostlock_app_jni_Native43499Jni_*`，与 Kotlin 9 个 `external fun` 集合完全一致；旧 Flow/session/route/registry 符号=0；`.data/.bss` 可写 OBJECT 符号=0（仅 relro/fini_array/dynamic/got.plt 链接器段）。PASS。
- **JNI↔Kotlin 字段对拍**：JNI 写 42 字段、读 41 字段，全部存在于 Kotlin DTO；无旧 `generation` 等字段残留。PASS。
- **九入口行为**：`api_unavailable.c` 9 个唯一强定义，LLB 合法 ABI+非零 token ⇒ `UNSUPPORTED`/handle=0/effect=NONE；ABI 无效 ⇒ `BAD_ABI`（不越界写）；token=0 ⇒ `INVALID_ARGUMENT+NOT_APPLIED`（未接受）。本批不创建 fd/pid/thread/map/非零 token。PASS。
- **产物身份**：APK `build/app/outputs/apk/release/GhostLock-v1.3(1338)-arm64-v8a-release.apk` sha256=`4b02dce2…`，172 entries、3 个 arm64 DSO；APK 内 `libghostlock43499.so` sha256=`9da04e2a…` = `stripped_native_libs/release` 输出；未剥离中间物 `3486e6b4…`；debug DSO `3276401e…`。PASS。
- **临时物**：批次代码/文档内 TODO/FIXME/旧码注释块/临时探针/旁路=0；`using` 新增=0。
- **link map**：仓库/Gradle 未生成 link map ⇒ plan §6 该项不可核验，记录为 plan 缺口（并入 B2）。

## 4. 非作者评审（两岗，只读，冻结快照）

- **design-critic（常驻岗）**：总判=**不可**；阻断 3 / 重要 8 / 建议 9。阻断：B1 四件冻结物覆盖不到最终树且 VERIFICATION 预写评审结论；B2 §6 门禁命令不可执行且未登记 T4；B3 工作树/索引不干净、构建产物与幽灵条目存在。重要：批准表 §4.1/§5 标题与已批准行语义冲突（I1）、批准状态多处并行声明（I2）、批次依赖图三处不一（I3）、owned-entry 接受点措辞不完整（I4）、migration guide 旧 registry/borrow 设计未降级（I5）、`content_generation` 无语义（I6）、guide §4 证据位置与仓库实践矛盾（I7）、guide §2.3 负例门槛与 D1 v0.2 冲突（I8）。
- **native-core（只读攻击资源岗）**：总判=**可进 Lead 最终 gate（native 契约判据）**；阻断 0 / 重要 4 / 建议 5。重要：close/cancel 回执矩阵未冻结（N1）、`content_generation` 无语义（N2）、execute 一次性语义未冻结（N3）、冻结包/回滚证据不一致（N7）。资源表（G4L-0 无物理资源）逐项以 N/A+证据闭合；ABI 布局、9+9 符号、无全局、JNI 无 profile/route/地址算术/payload 解释等均独立复核 PASS。
- 两岗均确认评审窗口内目标文件无写入；我方 16:42 的 Gradle 构建写入 `app/.cxx` 构建产物（非源码），已在 B3 归类。

## 5. Rollback 实测（独立副本）

- 对「四件自身快照」（HEAD+DIFF_FILE）回滚：`ROLLBACK.sh` exit 0；13 tracked 恢复为 HEAD 逐字节、新增计划被删除 ⇒ PASS。
- 对「当前终验树」（含计划 2 行指针、指南）：`ROLLBACK.sh` exit 1；13 tracked 仍正确恢复，但计划文件残留并产生 `.rej`（`patch` 判为 already applied/reversed）⇒ **FAIL**，即 B1/N7：冻结四件不覆盖最终交付树。
- 仓库内未产生 `.rej`/回滚残留（实测仅 /tmp 副本内）。

## 6. 资源表（G4L-0 适用性）

G4L-0 只改契约/transport，不创建任何物理资源。Placement/Page/Actor 的 create/owner/borrow/terminal/releaser/failure state 均为契约陈述，native-core 已逐项以 file:line 闭合；本批「创建」列全部为 stub 返回 0。辅助资源（JNI 临时 frame/payload 拷贝）有 malloc→clear_free 配对；全局/共享资源=N/A（nm 无 DATA/BSS、无 fd/pid/thread/map syscall）。本批无 UAF 面。

## 7. 判定与不得前进的理由

- 判定 **FAIL**。依据：①plan §6 门禁 2 不可执行、门禁 4 红且未登记例外（B2）；②四件冻结证据与 rollback 不覆盖最终树（B1/N7）；③工作树/索引不干净，guide §5.1/§5.2 第 1/2 项不能通过（B3）；④AGENTS §7「阻断未关不得进批准 gate」，design-critic 阻断 3 未关闭。
- **不得请求进入 G4L-1**（guide §5.10：只有 PASS 才能请求下一批）。G4L-1 及以后保持未批准/只读勘察状态。
- 当前入口状态：九个 43499 入口全部 `UNSUPPORTED`（fail-closed）、**实验契约**，未过真机、非 supported；G4L-0 内容级（契约文本/transport）与 HEAD 行为等价（仅字段改名/注释/接收点语义文字），但不得据此宣称批次通过。

## 8. 返工建议（需用户裁定后由 Lead 重新冻结范围）

1. **R1（B1/N7）**：以最终树重生成四件（DIFF_FILE/MODIFIED_FILE/VERIFICATION/ROLLBACK.sh，含计划+指南），隔离副本重跑回滚；删除/归档已被取代的旧四件与旧批 artifacts 残留。
2. **R2（N1–N3/I4/I6）**：冻结契约文本——close/cancel 回执矩阵（含 released XOR retained、cancel 不消费）、`content_generation` 语义（不参与接受/生命周期）、`execute_actor` 恰好一次、转移入口 vs close 入口两类接受点；同步 plan/guide/UML。
3. **R3（B2）**：修正 plan §6 为当前可执行 Gradle 门禁并登记 T4 降级（谁批/缺项/偿还时机）；5 个先存红测试需用户裁定：另立修复批 / 明确批准门禁例外 / 暂停。
4. **R4（B3/I7/I8）**：工作树/索引卫生方案（构建产物跟踪、幽灵索引条目、证据目录仓库内外规则）与 guide §2.3 负例口径对齐 D1。
5. **R5（I1–I5/S1–S9）**：批准状态唯一权威收口、批次依赖图单点权威、migration guide/rewrite §5.2 旧设计降级、评审记录回填（design-critic/native-core 行）。

## 9. 已验证 / 未验证

- 已验证：本文 §1–§6 全部条目（命令 literal output/exit 见仓库外日志 `/tmp/g4l0-lead-20261026/`，hash 附后）。
- 未验证：真机门禁（G4L-0 不涉攻击资源，不适用）；JNI↔Kotlin 运行期字段绑定（无真机/无 R8 产物运行）；link map（无生成物）；native host CTest（任务不存在）。

## 10. 用户裁定（2026-10-26，终验后）

1. **G4L-0：暂停，仅保留本 FAIL 记录**。不启动返工、不申请进入 G4L-1；G4L-1 及以后保持未批准/只读勘察。
2. **门禁政策（方向，随暂停未生效；恢复时按此执行）**：修正 plan §6 为当前可执行 Gradle 门禁并登记 T4 缺口；`PayloadRequestsTest`(3)、`ScriptRenderTest`(2) 这 5 个先存失败另立独立修复批，修绿后才允许 G4L-0 终验 PASS。该判据变更届时须由 Lead 重新冻结范围并留沿革记录。
3. 本裁定不构成对 G4L-1 或任何物理实现的批准；plan §8 停止条件继续有效。

## 11. 证据路径

- 仓库外 Lead 日志目录：`/tmp/g4l0-lead-20261026/`（`run-gates.sh`、`plan6-line2.log`、`plan6-line3.log`、`testDebugUnitTest.log`、`head-baseline-test.log`、`rollback-run.log`、`rollback-exact.log`、`dso-exported.txt`、`apk-sha.txt`、`layout_*.txt`、`snapshot-hashes.txt`、`final-snapshot-hashes.txt`）
- 四件（旧，待 R1 重生成）：`docs/analysis/artifacts/43499-linear-owner-revision-20261026/`（DIFF_FILE `e4b88c00…`、MODIFIED_FILE `0800c8ae…`、ROLLBACK.sh `d2cea7f1…`、VERIFICATION.txt `09b6a09e…`）
- 评审记录：本文件 §4（完整评审原文在会话消息与两岗 task-1/task-2 记录）。
