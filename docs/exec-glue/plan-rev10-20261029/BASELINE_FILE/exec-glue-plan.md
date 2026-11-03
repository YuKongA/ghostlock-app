# Exec Glue 部署计划（唯一活动版）

> 状态：rev9 source-backed handoff 方案待 design-critic 复核；rev8 已评为阻断（3 阻断、2 重要、1 建议）。新增 worker/Flow 状态源代码对拍；生产代码门关闭。目标版本：2.0.12。
> 取代关系：本文件是唯一活动计划；此前批次记录、设备证据与详版 rev1–rev4 见归档 docs/archive/20261029-1025-exec-glue-plan-history.md。
> 依据：docs/development/软件工程守则.md、design-philosophy.md、engineering-standards.md、design-review-principles.md。

## 1. 目标与边界

在 dev-nocpp 工作树中交付同级 Glued 后端 43499-Glued 与 43284Glued。App 的 SequentialFlow 各只有一个 exec step；Shizuku 是可选 launcher：授权且可达时只选 Shizuku，否则选 App；已选 launcher 的启动结果不确定时不得再启动第二个 child。现有 UI 页面和控件保留，原版 CVE 后端与 Custom Lua 灰显不可选，原版 native targets 不构建。43499 上游 C/C++ 保持不变；43284 上游 C/KO 仅允许为 root handoff 作最小、可追溯的改动；不 commit/push。

本次只修订 root 执行权与 Finish 终止语义，不改攻击原语、profile schema 或既有 UI 布局。生产批次须先经设计 gate 和用户批准，再单批实现、验证。

## 2. 已确认的用户决策

- Safe mode 开关生成本次运行唯一 effective profile；43499 GLK1 meta.safe_mode 与 root script 使用同一值。
- Root Manager 不增加 Glued 专属候选搜索/优先级；common Payload/Finish 不依赖管理器 su，也不打包 su。
- 用户 2026-10-29 明确允许修改 43284 上游 C/KO，仅用于加入 root handoff；不得据此扩展攻击原语或其它上游改写。Shizuku 不是必需，有授权走 Shizuku，否则走 App launcher；launcher 选择不等于 root 授权。
- Finish 可包含软重启；Finish 是本次业务流程的最后动作，之后不追加 marker、日志写入、清理或其它业务动作。

## 3. rev9 设计合同

### 43499-Glued

Profile 先解析，再按 Safe mode 开关派生 effective profile；源 conf 不改，GLK1 与脚本使用同一值。Flow 为每次 run 生成固定 allowlist 文件和 UUID runDir；root script 预置于 native 预期路径，作为 regular 0444 文件。native O_TRUNC 写失败后仍由 rooted victim 按同路径只读打开并 exec；用无副作用 fixture 验证 inode、owner、mode 和内容 hash。

脚本镜像 ops.cpp:236-431 的 root gate、iomem/debug、policy fixup、KMI、safe-mode、module wait、trap 和失败状态。KSUD 搜索/fallback 从 Glue 路径移除。已选 RootManagerSpec 的单一 package 由 PackageManager 得到 nativeLibraryDir/libksud.so；执行前核验绝对路径、regular file、可执行位、目标 ABI，以及 App/Shizuku 两个 victim 执行域可达性。校验失败即 fail-closed；不枚举候选、不 fallback。FolkPatch 只走 common launch-only/LKM 语义，所需 apd/LKM 条件不成立时 readiness fail-closed。

执行顺序：profile/root gate → SELinux policy fixup → common RootManager direct-path late-load（恰一次）→ KernelSU module wait/restore → 其它 payload 与状态恢复 → common Finish（最后业务动作）。管理器 binary 只承担显式 late-load，不承担 Payload/Finish 提权。若 Finish 启用软重启，重启请求是脚本最后命令；之后无 marker/log/cleanup/其它工作。Finish 输出目标在启动前打开并落盘；root script 在交接 Finish 前完成所有状态/资源操作，再以 exec 替换 shell 进入 common Finish。exec 成功后不观察、不写 marker/log、cleanup 或其它命令；App 只在发起 handoff 前记录 requested，不记录成功。exec 失败才走 Finish 未启动的失败分支。上游 C++ 不改。

### 43284Glued

Flow 仍只有一个 SequentialFlow exec step；App/Shizuku 只负责启动上游 ELF，不作为 root 来源。当前 libcxx.S 入口先检查 uid/tid，再通过 insmod 进入选中的 DFRoot KO；KO 中的 usermode-helper 是 root handoff 候选。现有 bootstrap.c 只读 DFRoot prefs、调用 ksud late-load，并可自行触发 soft reboot；没有 common Payload/Finish 请求接口。现有 exp.c 把 /dev/dfm6 当成功后返回，随后 main() 调用 cleanup()；Finish 前必须完成 native hook 恢复。

**root-handoff 候选设计：** DFRoot v4.7 上游 commit `49e992ef3c9f7ec03c8efea6e3cf552f7ec6a347` 的实际链路已定位：`lkm/dfroot.c:55–61,161–183` 以 `/system/bin/sh` 调 `call_usermodehelper_setup/exec(..., UMH_WAIT_EXEC)`，设置 init exec-context 后执行固定 `/data/user_de/0/df.root/bootstrap`；`ExploitRunner.java:115–126` 将 DFRoot 自己的 `libbootstrap.so` 复制到该私有路径。GhostLock APK 中的 `libbootstrap.so` 并不会因此自动成为 UMH 目标，且不得写入 DFRoot 私有目录。DFRoot README 的 How it works §5 将模块/UMH 进程记为 `uid=0`，代码运行时仍必须验证 `getuid()==0 && geteuid()==0`。

不要把现有 libbootstrap.so 当作 root worker：43284glued/CMakeLists.txt 当前由上游 bootstrap.c 生成它。新增 Glue 专用 root_worker.c executable target，暂定 APK 名 lib43284GluedRootWorker.so；以本次 launcher 的 nativeLibraryDir 绝对路径作为 UMH helper，保留上游 bootstrap.c 原文/来源记录；它不作为本次 root-worker 或 common Payload/Finish 执行入口。当前 libcxx.S:114 的 insmod argv 只有工具、KO path、NULL；exp.c:500-531 只接收 IPsec 参数并改写 KO path；pinned upstream KO 没有这些 module parameters。用户授权允许把有界 worker_path/run_dir/socket_name 从 host argv→exp.c→汇编 insmod 参数→新增 KO module_param_string→UMH argv 串起来，但 ABI、上限、编码、错误语义和逐 KMI 重建仍是待定设计门。固定 shell 语句只用 positional argv，不拼接动态值。AVD 的库抽取/权限事实不证明 helper exec 或双 launcher 可达性。

Payload、Finish、log 在 launcher 所属 runDir 中预先落盘；socket 仅承载有版本的 ROOT_READY(runId,pid)/单次 release 控制帧，不传脚本内容或命令。Flow 顺序仍为单个 SequentialFlow exec step：listener/staging → 单次启动 ELF → worker ready → ELF 自然退出并完成 exp.c cleanup → launcher 关闭 IpSecRuntime → UI/Flow 进入明确的 HandoffRequested 状态 → 发 release → root worker 执行 Payload → 关闭非继承资源 → execve(Finish)。成功 exec 后 worker 不返回，App/launcher 不等待、观察、写日志或清理；exec 失败且 Finish 未启动时才报告失败。当前 BackendRunUiState 只有 Idle/Running/Finished/Refused；Flow43284Glued 以子进程退出映射 StepResult，RunPage 把 FlowResult.Ok 写成 backend run: ok。实现须增加不等于 Finished/成功的 HandoffRequested 展示路径，覆盖 dispatcher、ViewModel、Material/Miuix 两处 RunPage 与测试；保留现有 UI 元素，不增加 Finish 后完成标记/成功状态。候选命名 HandoffRequested，须 rev9 critic 收敛。错帧、身份不符、超时、断连和重复请求均 fail-closed；runDir 留给下一次身份核验。

**实现前必须闭合：** module parameter 的生产 ABI/边界及每个 KMI 的实际 insmod argv→KO→UMH 数据链；单独 root worker ELF 的路径/owner/mode/SELinux/namespace；AF_UNIX 创建者、地址、SO_PEERCRED、peer UID 与 App/Shizuku 双域可达性；exp.c cleanup 对 worker/socket 的所有权；HandoffRequested 如何先于 release 呈现且不落成 success；execve 成功无后续动作、失败才报告；每个 KO 原 SHA、patch 和替代产物 hash。Host fixture 不替代 A301SO 设备证据。

### 执行结束与资源

Finish 是最后业务执行体。日志目标、脚本文件、fd、锁、IpSecRuntime 和 staging 检查均在启动 Finish 前完成。Finish stdout/stderr 直接写入预先打开的落盘目标；成功 exec Finish 前关闭控制 socket/listener、锁、IpSecRuntime 和非继承 FD。exec 成功后进程映像属于 Finish，App/launcher/worker 不等待返回、不观察退出、不写状态/日志、不 cleanup、不执行其它业务；Finish 触发软重启也没有后续步骤。仅当 exec 失败且 Finish 尚未启动时，才允许报告启动失败。

runDir/log 由本次流程保留；下一次运行的独立 preflight 才可回收旧目录。回收前按 runId 核对 direct child、detached worker、输出句柄均已结束，并复核目录/文件身份；只凭年龄、状态或 marker 不得删除。不可观测、身份不符或仍存活的执行者均保留。Unknown 目录不得删除；容量/回收 preflight 必须早于 runDir 创建、staging 与 launcher 启动；同一 launcher root 的并发 preflight 串行化，无法取得唯一处理权则 fail-closed。配额超限时拒绝新 run、返回具名原因并提示人工处理，不创建 runDir，也不自动删除 Unknown/活跃目录。43284 的 IpSecRuntime 必须在 root worker 接收 RELEASE_FINISH 前关闭。App 与 Shizuku 不共享绝对路径；两端独立 staging 并验证 regular/no-symlink、owner、mode、hash。

## 4. 允许修改范围

- 43499：profile codec/Flow、backend-specific root-script generator、common FinishScript/PayloadRequests strict-path 接口、run-file stager/process/host。
- 43284：允许最小改动上游 `lkm/dfroot.c`（KO）、`exp.c`、必要的 `libcxx.S` 与 Glued root-worker C 文件；同时允许对应 CMake、source manifest、Flow/host/stager/root-script bridge。具体文件、module-parameter ABI 与执行序须由 rev9 design-critic 逐项收敛。
- 对应 JVM/Android tests；状态增加涉及 BackendRunUiState、BackendRunGate、BackendRunViewModel 与 RunPage 双 UI 分支；结构更新同批改 docs/development/full-process-uml.md 的 exec/Finish sequence 与所有权/生命周期图。
- 保留：UI 布局、原版 backend 实现、43499 上游 C/C++、LegacyProfileConverter 转换语义、已有 LKM Finish 路径；43284 pristine upstream 来源与原始 manifest 哈希始终保留，Glued 改动不得覆盖来源副本。

## 5. 设计与实现门

1. rev4 的 su -c/Finish 后 marker 方案作废。rev9 critic 复核 helper/ABI 数据链、固定 argv root-worker 装载、Unix-domain control-only 协议、euid/SELinux 证据、App/Shizuku 两域可达性、HandoffRequested UI/Flow 状态和单次执行。Finish 成功执行后不等待或观察其结果，不写状态；APK 内置 su 不属于设计。
2. Finish 是最后动作：日志目标和继承 FD 在执行前建立；执行成功后不等待、不观察、不追加 marker/log/cleanup/状态，也不记录 Finish 成功。UI/Flow 只表达 handoff requested，绝不映射成 Finished(FlowResult.Ok)。handoff 状态转换须在 release 前呈现；release 后无业务回调。旧 runDir 只在下一 run preflight 核验 runId、进程身份与文件身份后回收；不能证明执行者已结束时保留目录；配额超限拒绝新 run并提示人工处理。
3. rev9 design-critic 的 helper/ABI、socket/SELinux、upstream/KO hash 与 Flow-state 阻断关闭并复核无阻断后，仍须用户明确批准才可开始生产源码实现。计划 ≤16 KiB；43499 direct-path 与脚本 parity 是独立实现门。
4. 分批实施：先 43499 root-victim bridge；43284 单独一批实现 root callback、launcher rendezvous、Payload→Finish exec handoff，先跑无副作用 fixture，再按攻击路径门禁逐项关闭。每批更新 UML、所有权/生命周期表、测试和记录，不跳门。
5. Host：针对性测试、完整 testDebugUnitTest、assembleDebug、assembleDebugAndroidTest、上游源 SHA 对拍、APK/ELF ABI/entrypoint 和 KO patch/hash 校验。
6. 设备：当前 AVD emulator-5554 online，A301SO 当前不可见。先在 AVD 验证 UI 与无副作用 launcher/Finish fixture；A301SO 重新连接后验证 APK 身份、launcher uid/context、root worker uid/context、socket、log 与脚本 staging。未经专门真机门禁不运行生产漏洞 ELF；不以 AVD/fixture 结果宣称 production exploit supported。

## 6. 当前阻断与证据

- 用户 2026-10-29 新确认：允许修改 43284 上游 C/KO 加 root handoff；common Payload/Finish 不依赖 manager su；Finish/soft-reboot 后不追加动作。该授权仅变更设计边界，不等同于生产实现批准。rev6 critic 结果不覆盖 rev9；rev8 critic 结论见下方。
- DFRoot 上游证据：`/tmp/DFRoot-upstream-inspect` pinned HEAD/tag `49e992ef3c9f7ec03c8efea6e3cf552f7ec6a347 v4.7`；`lkm/dfroot.c` SHA-256 `fc1cdb216b3364bd408acd76bb7943bcb6f5f257ec134962df96ac97b6b460be`。`bootstrap.c` SHA-256 `5b19bfe9d4c632794d52de71a513d1bac2820e0fbd474ce19813913782111ce9`，当前 Glued `exp.c` SHA-256 `afdd68cb6b07fa9b04f93c7d4bd9f8ce0333f4dd2265416959cf9c630d55ac39` 与 pinned commit 逐字节一致；upstream CMake SHA-256 `e004a88b1b9a383ac8e092f22a6dfbb2e60404f4dafdcca3cde84afd7e00f2b6`，当前 Glued CMake SHA-256 `ca920d671a8af374f61edcc803b894982ed161302487a050b5b4744eb968a6b3` 是构建适配，不能以其与上游不同判定 C 原文失配。旧 `SOURCE-MANIFEST.json` 的 CMake/exp 期望 hash 分别为 `c5b964f4c4084c56b862ee337cbb637227d40c5fa381cb3c77f4b6c909f7590d` / `22a905982819e94b908b298f930aa821c60d98bad97425e534cb9c9ebef631ef`，均不匹配 pinned v4.7；`df_log.c/.h` 不是该 upstream commit 文件。保留旧 manifest 原字节，另建准确的 pristine provenance manifest，并复制 pinned 上游文件供后续逐文件 patch 对拍。
- KO 来源证据：8 个当前 KO 文件与 `/Users/nickji/Documents/Program/Analysis/DFRoot` v4.6 checkout 的忽略构建产物逐字节一致；上游 `lkm/Makefile` 与 `.github/workflows/build.yml` 显示 KMI matrix 和 DDK image `20260828`。尚未从可复核的 pinned workflow artifact 对拍这些 KO，也未为新增 KO patch 生成替代字节及逐变体 hash。
- 当前源码对拍：43284glued/CMakeLists.txt 把上游 bootstrap.c 编成 libbootstrap.so，与新 root worker 身份不同；libcxx.S 当前 insmod argv 无 module 参数；exp.c 只改写 KO 目标路径；BackendRunUiState 无 handoff 状态且 RunPage 对 FlowResult.Ok 显示 backend run: ok。AVD 抽取的 libbootstrap.so 为 12,680 B、0755、system:system 只证明打包/抽取，不证明 UMH 执行、root UID/context 或 socket 权限。
- 43499：方案顺序已闭合为 common RootManager 单次 direct-path late-load；实际 libksud 身份/ABI、双 launcher 可达性、FolkPatch readiness 与失败转移仍是实现/设备门。
- AVD 当前 UI/selector 与 2.0.12 APK、profile source、已有 safe fixture 结果见归档详版与 docs/exec-glue/ 下对应 transaction；最近 adb devices -l 见 emulator-5554 online；未运行生产 Glued ELF。
- rev8 design-critic：快照 SHA-256 7844af3d874a084c00f95030f6f2737e7551fce31b4b174165120b031c8a5c4b · 14,910 B；总判阻断（3 阻断、2 重要、1 建议）。Finish no-return 语义通过，但 helper ABI/SELinux/KO provenance 阻断未关闭；此结果不覆盖本 rev9。
- 2026-10-29 rev9：对照当前 CMake、libcxx.S、exp.c、43284 Flow、BackendRunUiState 与 RunPage 后，纠正 root worker 不应复用上游 libbootstrap.so，并新增 HandoffRequested UI/Flow 方案。仅设计；等待 rev9 design-critic，未执行 Glued ELF。
