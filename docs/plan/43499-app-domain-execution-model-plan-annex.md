# 43499 纯 app 域执行模型 L 设计附件（v4 冻结后的 I3/I4/I5 细则）

- 状态：**实现批前置文档收口（task-51），只写设计、零实现、不 commit**。design-critic 第三轮对主件 v4 判 **可进实现批（阻断 0）**；本附件收口 I3（E4 零 fork 证明）、I4（E5 与⑥边界）、I5（file_actions 冻结），**不改变可进结论**。
- 主件：`docs/plan/43499-app-domain-execution-model-plan.md` **已按 v4 权威冻结**（历史身份：16379 B，sha256 `2a7ff267c3f72630c134a890e5390f062c40d8238747b36021a83dd18f3e76f5`；字节数不构成限制）。I3/I4/I5 的职责已移交本附件，因此主件不再扩写；旧 5 B 余量门已于 2026-11-03 取消。本附件为 I3/I4/I5 的**唯一细则权威**；与主件冲突时以本附件对 I3/I4/I5 的收紧条款为准，其余以主件为准。
- 日期 2026-10-27；作者 routes-impl2；依据：主件 v4、三轮 critic I1–I5、approval §6.8、用户 U1–U4 裁定。
- 文档不设字节或篇幅门；本附件按职责与主件分离。主件冻结、approval §6.8 冻结正文/只 append（见 approval 记录）。

## A（I3）E4 零 fork 证明：①②③为 PASS 主证明，④仅诊断

**PASS 主证明（必须全过；tracefs 不可用时判定不变）**

| 项 | 证明方法 | 判据 |
| --- | --- | --- |
| ① link-time wrap 计数 | **Debug 测量构建**（`CMAKE_BUILD_TYPE=Debug`）的 app DSO/helper：`-Wl,--wrap=fork -Wl,--wrap=vfork` + `GLK_VICTIM_FORK_WRAP=1`；`__wrap_fork/__wrap_vfork` 只 `__real_*` 转发并原子计数；E4 运行结束经 JNI 回执上报两处计数（app + helper）。**Release/RelWithDebInfo 生产构建不传 wrap、不定义宏；计数器 API 恒 0/no-op**。 | **app 计数=0 且 helper 计数=0**（E4 在 Debug 测量构建执行；helper 侧只允许 posix_spawn；execScript 亦 posix_spawn） |
| ② 符号审计（严格口径，task-60） | 对 **Release/RelWithDebInfo（生产构建，无 wrap）** 的 app DSO 与 helper：`readelf -Ws`（或 `llvm-readelf -Ws`）无 `fork`/`vfork` 符号（UND/DEF 均禁止）、无 `__wrap_*`/`__real_*`；`posix_spawn` 必须在；`clone` 允许（bionic 内部）。可复跑：`python3 tools/verify_no_fork_imports.py`（自动定位 Gradle Release/Debug 产物 + NDK llvm-readelf/llvm-nm，`nm -D` 交叉）。**Debug 测量构建按构造出现 fork/vfork 导入与 `__wrap_*`，只作 ① 测量、不作 ② 证据**。 | Release 两文件均无 fork/vfork 导入且 `posix_spawn` 在（出现即 FAIL）；Debug=INFO 不计判据 |
| ③ 源码调用图审计 | 对 v4 实现批新增/触及源文件做 `fork(`/`vfork(`/`clone(` 语义审计（grep+人工调用图），只允许 bionic `posix_spawn`；审计清单随实现批复核记录归档 | 无 fork/vfork 调用；`clone` 仅出现在 bionic 库内部（不自写） |

**② 口径收紧（task-60，2026-10-28）**：此前 `--wrap` 对 Debug/Release 无条件生效，Debug 产物
`fork_imports=2` 使严格 ② 无法在 Debug 上判 PASS；现 CMake 以 `$<CONFIG:Debug>` 只在 Debug 链接 wrap，
Release/RelWithDebInfo 不传 wrap、不定义 `GLK_VICTIM_FORK_WRAP`；严格 ② 只在 Release 产物上判。
E4 ① 计数测量使用 Debug 构建（`GLK_VICTIM_FORK_WRAP`），`tools/verify_no_fork_imports.py` 输出 Release PASS
与 Debug INFO 分栏证据。

**④ `sched:sched_process_fork` tracepoint = 仅诊断，不参与 PASS/FAIL**：bionic posix_spawn 内部用 `CLONE_VM|CLONE_VFORK`，同样触发该 tracepoint，无法区分 fork(2)，故**不得**用其计数作通过判据。可选辅助断言（仅实验期 root/tracefs 可用时执行、失败只记 evidence 不改判）：以 parent pid 过滤 tracepoint（`perf script` 中 `parent_pid == app_pid`），当窗口内只有 E4 已知 spawn 时，断言 **events == expected_spawn_count（K）**；出现「parent=app 且 K≠预期」或「parent 非 app/helper 的 fork 事件」记 WARN 并归档，不影响 ①②③ 判定。tracefs 不可用 ⇒ 跳过④，只按 ①②③ 判 PASS/FAIL/不可判定（①②③任一无法测量⇒不可判定，需重测）。

## B（I4）E5 功能性 PASS 与 ⑥ 冷机重复可靠性 PASS 的边界

| 门 | 定义 | 判据 | 失败处理 |
| --- | --- | --- | --- |
| **E5（功能性）** | J 实现批完成后的一次**功能性全链**验证（单次冷机、单 route） | step 边界独立判定：W1(JNI)→helper self-perf(posix_spawn)→Kotlin 解析/投票/选址→JNI W2/W3→finish(rooted helper exec 脚本)→enforce 恢复；**每 step 均 PASS 且整链无 panic/资源终结** ⇒ E5 PASS | 任一 step FAIL ⇒ E5 FAIL、停并归档该 step（不启动⑥）；**不要求重复次数** |
| **⑥（可靠性）** | E5 PASS 后的**冷机重复可靠性**门（生产启用硬门） | 冷启动重复 **N=3**（初值、Kotlin 参数化；每次均冷机/固定 CPU/KSU 未加载/单 route/pstore 检查）；**N 次全部功能性 PASS 且无 panic** ⇒ ⑥ PASS；每次结束后 enforce 恢复核验 | 任一次功能 FAIL/panic ⇒ ⑥ FAIL（E5 功能性结论保留，不推翻）；生产启用禁止，route 保持 UNSUPPORTED，登记失败证据 |
| 依赖 | E5 依赖 E1–E4；⑥ 依赖 E5 | — | E5 FAIL ⇒ ⑥不启动；⑥ FAIL ⇒ 仅阻塞生产启用（E5 结论仍为“功能性 PASS”，实现批可继续 debug 调参，不得标 supported/不得 fallback） |

**不合并且说明**：两门分开保留（可诊断性：功能缺失 vs 概率/冷机问题）；若后续 Lead 决定合并，必须是「单一门 + 两个独立 verdict 维度（functional/reliability）」，不得以可靠性 PASS 掩盖功能 FAIL。P0c 顺序相应为：…→⑤ E5 功能性 PASS→⑥ 冷机重复可靠性 PASS→⑦ 生产启用硬门（G1'）。

## C（I5）file_actions 冻结：`POSIX_SPAWN_CLOEXEC_DEFAULT` + 显式 dup2（或受控 addclose）

**C1 spawn helper（唯一允许的两种实现，实现批二选一并在批复核记录冻结）**

- **方案 A（优先/默认）**：`posix_spawn` flags 含 `POSIX_SPAWN_CLOEXEC_DEFAULT`（除 0/1/2 外全部关闭）⇒ file_actions 只需 `adddup2(controlSrc, 3)`、`adddup2(rawSrc, 4)`；**不调用 addclose**（CLOEXEC_DEFAULT 已关闭其余 fd）。
- **方案 B（平台/API 不支持 A 时）**：file_actions 限定 `addclose(orig)` + `adddup2(orig, target)`，且必须满足：①`addclose` 只针对**原始 fd**（`controlSrc`/`rawSrc`）；②原始 fd **≠3 且 ≠4**（若碰撞，先用 `F_DUPFD_CLOEXEC` 把原 fd 移到 >4 再 dup2）；③**addclose 在对应 adddup2 之前**执行（否则会把新安装的 3/4 关掉）；④其余继承 fd 由 helper 启动 `close_range(max(3,4)+1, ~0)` 防御性关闭（双保险；见主件 §5.2）。
- 两种方案共同冻结：**child exec 后 fd 最终集合 = {0,1,2,3,4}**（0/1/2 stdio、3 control、4 raw）；helper 启动自检 `fd≥5` 的 `fcntl(F_GETFD)` 必须 EBADF；fd 泄漏计数=0（spawn/close 后 `/proc/self/fd` 差）。
- file_actions 失败码（机械回执，不猜业务）：`victim-spawn-fileactions-init:<errno>`、`victim-spawn-fileactions-dup2:<which>:<errno>`（which=control/raw）、`victim-spawn-fileactions-addclose:<which>:<errno>`；失败时关闭已建 parent fd 并返回具名 call_status/system_error，Kotlin fail-closed。

**C2 execScript（helper 内，rooted 后仅脚本执行）**

- flags 含 `POSIX_SPAWN_CLOEXEC_DEFAULT` ⇒ 脚本进程 fd 集合**仅 {0,1,2}**；file_actions 为空（不做 dup2/addclose）；不使用 helper 的 3/4。
- 失败码：`victim-exec-script-spawn:<errno>`、`victim-exec-script-fileactions:<errno>`（若 init 失败）；脚本 exit code/输出摘要独立回执；**execScript 不得启动攻击载体**（仅 `/system/bin/sh <script>`）。

**C3 fixture（实现批单测/设备测试共用向量）**

| 阶段 | 期望 fd 集合 | 断言 |
| --- | --- | --- |
| parent spawn 前 | app 既有 fd（不可枚举全量） | 记录 `/proc/self/fd` 计数基线；spawn 后 parent 无新增泄漏 |
| child exec 后（helper） | `{0,1,2,3,4}` | `/proc/self/fd` 精确集合；fd≥5 `fcntl(F_GETFD)`=EBADF |
| helper close/bye 后 | parent 恢复基线 | spawn/close 前后 `/proc/self/fd` 计数差=0（fd 泄漏=0） |
| execScript 子进程 | `{0,1,2}` | 脚本进程快照（helper 内 `/proc/<pid>/fd` 读取或脚本自证）；3/4 不存在 |

fixture 以**命名向量**入实现批单测（host：file_actions 参数/顺序/失败码编解码；设备 E4：真实 `/proc` 快照），禁止硬编码 3/4 之外数字；`3`/`4` 为协议 ABI 常量（文档冻结值）。

## D 冻结/append/拆附件记录（I1/I2 对应）

- **主件冻结**：`43499-app-domain-execution-model-plan.md`（16379 B，sha256 `2a7ff267…`）自 v4 起冻结，不再扩写；后续设计细则只入本附件；实现批若需设计变更须用户/Lead 另批并更新本附件（主件不再动）。
- **approval 冻结/append**：`43499-approval-status.md` §6.8 **冻结正文、只允许 append 行**；本附件的 bytes/sha 与 I3/I4/I5 收口在 approval 追加一行记录（owner=Lead 复核）。
- **拆附件**：2026-11-03 起主件与附件均不设字节/篇幅门；只按职责、读者与权威边界拆分。本附件不扩 approval 正文。
- **本附件唯一权威范围**：I3（E4 ④定位/辅助断言）、I4（E5 vs ⑥ 边界/依赖/失败关系）、I5（file_actions 方案 A/B/fd 集合/失败码/fixture）；其余仍以主件 v4 为准。
