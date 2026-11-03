# Exec Glue 部署计划（唯一活动版）

> 状态：rev11b/M0；生产实现仍阻断。M0 fixture-only 修复、exact-.189 host/APK 身份门和 reboot 基线已通过；instrumentation 因 A301SO `RUNNING_LOCKED` 未进入测试体，待用户解锁后重跑同一 App marker gate。目标 2.0.12。
> 取代关系：本文件是唯一活动计划；此前批次记录、设备证据与详版 rev1–rev4 见归档 docs/archive/20261029-1025-exec-glue-plan-history.md。
> 依据：docs/development/软件工程守则.md、design-philosophy.md、engineering-standards.md、design-review-principles.md。

## 1. 目标与边界

以 dev-nocpp 基线 commit b119085915748a940740d7ce919b5a30ed8e9346（2.0.12）交付同级 Glued 后端 43499-Glued 与 43284Glued。App 的 SequentialFlow 各只有一个 exec step；Shizuku 是可选 launcher：授权且可达时只选 Shizuku，否则选 App；已选 launcher 的启动结果不确定时不得再启动第二个 child。现有 UI 页面和控件保留，原版 CVE 后端与 Custom Lua 灰显不可选，原版 native targets 不构建。43499 上游 C/C++ 保持不变；43284 上游 C/KO 仅允许为 root handoff 作最小、可追溯的改动；不 commit/push。

本次只修订 root 执行权与 Finish 终止语义，不改攻击原语、profile schema 或既有 UI 布局。生产批次须先经设计 gate 和用户批准，再单批实现、验证。

## 2. 已确认的用户决策
- Safe mode 开关生成本次运行唯一 effective profile；43499 GLK1 meta.safe_mode 与 root script 使用同一值。
- Root Manager 不增加 Glued 专属候选搜索/优先级；common Payload/Finish 不依赖 manager su，也不打包 su。
- 用户 2026-10-29 明确允许最小、可追溯地修改 43284 上游 C/KO，仅为 root handoff；DFRoot system-root UMH 是候选，不调用 manager su。Shizuku 可选；授权且可达时用 Shizuku，否则用 App launcher，launcher 本身不授予 root。此项授权修改范围，不等于批准生产实现。
- Finish 可包含软重启；Finish 是最后业务动作。成功 exec 后无后续等待、观察、状态/日志/标记、清理或回调。
- 不增加额外命令行或 Finish 后状态/完成标记；UI 在交接前进入 HandoffRequested，之后不报告成功。

## 3. rev11 设计候选

### 43499-Glued
Profile 先解析，再按 Safe mode 开关派生本次唯一 effective profile；源 conf 不改，GLK1 与脚本用同一值。Flow 为每次 run 创建固定 allowlist 与 UUID runDir；root script 预置在 native 预期路径，为 regular 0444 文件。native O_TRUNC 写失败后仍由 rooted victim 按同路径只读打开并 exec；用无副作用 fixture 核验 inode、owner、mode、hash。

脚本镜像 ops.cpp:236-431 的 root gate、iomem/debug、policy fixup、KMI、safe-mode、module wait 与失败分支。移除 KSUD 搜索/fallback；唯一 RootManagerSpec package 的 nativeLibraryDir/libksud.so 在执行前核验绝对路径、regular、执行位、ABI 与 App/Shizuku victim 可达性，失败即拒绝，不枚举、不 fallback。FolkPatch 仅走 common launch-only/LKM 语义，前置条件不成立即拒绝。

顺序：profile/root gate → SELinux policy fixup → common RootManager direct-path late-load（一次）→ KernelSU module wait/restore → 其它 payload/状态操作 → common Finish。Manager binary 只做 late-load，不为 Payload/Finish 提权。launcher 先完成 staging 并把 stdout/stderr 直连磁盘日志，再将 UI/Flow 置为 HandoffRequested 并只启动一次 ELF；返回值不是 Done/Ok。子进程启动成功后不 wait、不读退出码/管道、不注册完成回调；只有尚未成功 spawn 时允许报告启动错误。root script 完成所有 Finish 前操作，清除可能在 shell 退出时运行的 trap，最后以 exec /system/bin/sh <absolute-finish-path> 替换自身。exec 成功后任何进程都不等待、观察、写标记/日志/状态或 cleanup。仅 ELF 尚未启动或 Finish exec 失败时可报启动失败；43499 上游 C++ 不改。

### 43284Glued
Flow 保持一个 SequentialFlow exec step；Shizuku 获授权且可达时作为 launcher，否则用 App launcher，两者都不授予 root。libcxx.S 先检查 uid/tid，再经 insmod 进入 DFRoot KO。现有 bootstrap.c 只读 DFRoot prefs、调用 ksud late-load，并可能软重启，不作为本次 common Payload/Finish 入口。exp.c 当前将 /dev/dfm6 当成功条件；改为仅接受本 run ROOT_READY 身份匹配，cleanup 返回成功后才让 ELF 以 0 退出。

**system-root 与参数 ABI 候选：** DFRoot v4.7 commit 49e992ef3c9f7ec03c8efea6e3cf552f7ec6a347 的 lkm/dfroot.c:55–61,161–183 以 /system/bin/sh 调 call_usermodehelper_setup/exec(..., UMH_WAIT_EXEC)，设置 init exec-context 后执行 DFRoot 私有 bootstrap；该 bootstrap 不会自动成为 GhostLock worker，也不写入 DFRoot 私有目录。新增 Glue 专用 root_worker.c executable（暂名 lib43284GluedRootWorker.so），连同 payload/finish/log 放进 launcher 自己的 runDir。候选 KO 参数：glk_worker、glk_run_dir 是绝对路径，仅允许 ASCII 字母、数字、斜杠、点、下划线、连字符且拒绝 .. 路径分量，各 1–255 字节（缓冲区 256）；glk_run_id 恰为 32 个小写 hex（缓冲区 33）；glk_launcher_uid 为 0–4294967295 十进制（缓冲区 11）。固定 insmod argv 顺序为 insmod、KO 路径、glk_worker=、glk_run_dir=、glk_run_id=、glk_launcher_uid=、NULL；Flow→exp.c→libcxx.S→KO module_param_string→UMH 各层校验长度/NUL/字符。参数缺失、空值、超长、字符非法均在 UMH 前以 -EINVAL 失败；UMH setup/exec 错误保留负 errno，不 fallback。每个 KMI 实测 insmod 接收和 KO 解码。UMH 用固定 /system/bin/sh -c 脚本 `touch /dev/dfm0; echo -n 'u:r:init:s0' > /proc/self/attr/exec; exec "$1" "$2" "$3" "$4"`，位置 argv 依次传 dummy、worker、runDir、runId、uid；禁止拼接动态 shell 文本。worker 核验 uid=euid=0、init context、自身及脚本身份。DFRoot UMH 成功启动后仍按上游约定以 -ENODEV 卸载 KO；这不作为 App 成功信号，唯一 ready 信号是匹配本 run 的 ROOT_READY。exp.c 不再把 /dev/dfm6 的 ksud SUCCESS 当作 Glued 成功。

**SELinux 硬门：** DFRoot 先尝试将 selinux_state 置 false；若读回仍 enforcing，必须确认 avc_has_perm 与 avc_has_perm_noaudit 都已有效 patch，否则在启动 worker 前 fail-closed。当前上游代码对缺失 hook 只记日志并继续，不能直接满足本合同。worker 还核验 u:r:init:s0、runDir owner/mode、无 symlink 与文件 hash；任何权限/context/身份不符都不接收 release。AVD 的库抽取属性不证明 UMH 可访问 App 私有目录或 Shizuku staging。

**目录文件交接（替换 rev10 AF_UNIX 候选）：** App 与 Shizuku 各自创建不可复用的唯一 runDir（mkdir 成功、launcher uid、0700、无 symlink），固定记录目录 dev/inode、owner/mode 与实际 SELinux file context；预先落盘 worker/payload/finish/log 与固定 ASCII v1 run manifest（version、runId、launcher uid、runDir dev/inode、每文件 dev/inode/owner/mode/size/SHA-256）并核 hash。root worker 以 openat(O_CREAT|O_EXCL|O_NOFOLLOW) 建 ROOT_READY，写固定顺序的 ASCII v1/LF 字段：version、runId、launcher_uid、worker_pid+starttime、runDir_dev+ino、worker/payload/finish_sha256；fchown 给 launcher uid、fchmod 0600、fsync 文件和目录。launcher 用 fstat+lstat 核全部身份字段；重复/旧 runId、目录 dev/inode 变化、owner/mode/context 不符均拒绝。ROOT_READY 先于 ELF 返回；exp.c 校验其 runId/uid/pid 后完成 hook cleanup，并将恢复失败作为非零退出。host 等 ELF 返回且 cleanup 成功后关闭 IpSecRuntime，Flow/ViewModel/RunPage 先进入 HandoffRequested（非 Finished/Ok），再用唯一临时名 O_EXCL/O_NOFOLLOW、fsync 文件、同目录原子 rename、fsync 目录生成一次性 RELEASE_FINISH；固定 ASCII v1/LF 字段依次绑定 runId、launcher_uid、worker_pid+starttime、runDir_dev+ino、manifest_sha256、payload_sha256、finish_sha256。worker 以 openat/O_NOFOLLOW 读取，比较 fstat+lstat 的 dev/inode、owner/mode、精确字段序列和 hash；读取后原子移除并 fsync 目录，拒绝缺失、重复、陈旧、替换、跨 run 或身份不符；将 payload 与 finish 流式复制到新建 root-owned O_EXCL 文件，核对复制内容 SHA 与 release 中摘要，保持只读 FD。worker 将 root-owned 副本 fchmod 0400，payload/finish FD 均用 O_NOFOLLOW 打开并按摘要复核；payload FD 清除 FD_CLOEXEC、lseek 到 0 后传给 /system/bin/sh /proc/self/fd/<payloadFD>，等待 Payload 返回，非零则不运行 Finish。Payload 成功后关闭控制及 payload FD，只保留 finish FD，清除 FD_CLOEXEC、lseek 到 0，再 execve /system/bin/sh /proc/self/fd/<finishFD> 替换自身，避免同 UID 路径替换影响脚本内容。exec 成功后任何层不等待、观察、回调、清理、写状态/标记/日志；UI 留在 HandoffRequested。释放前发生错误、超时、断连或 cleanup 失败时不发 release、不运行 Finish，保留现场。runDir 只由后续独立 preflight 在证明执行者已退出且身份吻合时回收。

**文件 vs socket 的 CBN：** AF_UNIX 需维护一个 listener/peer-credential 状态机及 connectto 域规则；目录方案增加两个原子 marker 文件和 fsync/rename，但复用现有 per-run staging，并避免跨 App/Shizuku 的 socket connectto 与 peer-credential 路径。选择目录方案；manifest/golden 固定两类 marker 的 ASCII v1 字段顺序、长度、hash、dev/inode、owner/mode 及拒绝原因。若 A301SO SELinux/DAC 实测不能证明两种 launcher 与 init worker 的必要读写，或身份/重放测试失败，则保持阻断并退回设计复核，不降级校验。

实现前须闭合：逐 KMI 参数 ABI/insmod→KO→UMH 实测；worker/runDir DAC、SELinux、namespace 和双 launcher 可达性；SELinux gate；exp.c cleanup 结果与 worker 等待/释放所有权；HandoffRequested 全链路先于 release；exec 成功无后续动作和失败分支；每个 KO 原 SHA、patch 与变体产物 hash。Host fixture 不替代 A301SO 证据。

### 执行结束与资源
Finish 是最后业务执行体。日志、脚本、锁和 IpSecRuntime 在交接前准备；stdout/stderr 指向预开日志。成功 exec 前关闭控制与非继承 fd，仅留脚本/日志 fd；之后不等待、观察、回调、写状态/日志/标记、cleanup 或执行其它命令。软重启仍是 Finish 自己的最后操作；仅 Finish 未启动的 exec 失败可报告。

runDir/log 由本次流程保留；后续独立 preflight 仅在确认执行者已结束、runId 与文件身份吻合时回收，否则保留并按配额拒绝新 run。preflight 早于 runDir 创建/staging/launcher 启动；同一 launcher root 并发处理串行化，不能取得唯一处理权即拒绝。不得仅凭年龄或 marker 删除 Unknown/活跃目录。43284 的 IpSecRuntime 必须在写 RELEASE_FINISH 前关闭；App 与 Shizuku 使用独立 staging，并核 regular/no-symlink、owner、mode、hash。

## 4. 允许修改范围
- 43499：profile codec/Flow、backend-specific root-script generator、common FinishScript/PayloadRequests strict-path 接口、run-file stager/process/host。
- 43284：允许最小、可追溯修改上游 lkm/dfroot.c（KO）、exp.c（argv/cleanup 结果）、必要的 libcxx.S，以及新增 Glue root-worker；允许对应 CMake、provenance manifest、Flow/host/stager。
- 对应 JVM/Android tests；状态变更涉及 BackendRunUiState、BackendRunGate、BackendRunViewModel 与 Material/Miuix 两处 RunPage；同批更新 full-process-uml.md exec/Finish sequence 与资源生命周期图。
- 保留 UI 布局、原版 backend、43499 上游 C/C++、LegacyProfileConverter 转换语义、现有 LKM Finish 路径；43284 pristine upstream 与原始 manifest 哈希保留，Glued 改动不得覆盖来源副本。

## 5. 设计与实现门
1. rev4 的 manager su -c 与 Finish 后标记方案作废。rev11 以目录文件控制帧替换 AF_UNIX；design-critic 复核参数 ABI、UMH 固定 argv、SELinux gate、marker 权限/身份和双 launcher 可达性、cleanup 所有权、HandoffRequested 与单次 release。
2. Finish 是最后动作：执行前备齐日志与资源；成功 exec 后不等待、不观察、不追加状态/日志/标记/cleanup，也不报告 Finish 完成。UI 仅保留交接前 HandoffRequested（43284 在 RELEASE_FINISH 前；43499 在 ELF spawn 前），Flow/UI 不映射为 Finished/Ok。任何 pre-release 错误都不运行 Finish；旧 runDir 只在下一次 preflight 证明执行者退出且身份吻合后回收，否则保留并按配额拒绝新 run。
3. M0：隔离 fixture ABI/KO/ELF/worker，测 A301SO init→App/Shizuku runDir DAC/SELinux 与 ROOT_READY/RELEASE_FINISH 单次消费；不复用生产 Flow、不读/执行 Payload/Finish、不报 backend success。worker 退出后同批删 APK/KO/worker/runDir/入口并验 absence；不批准生产实现。43499 parity 另门。
4. 分批实施：先 43499 root-victim bridge；43284 单独实现 root handoff 与 Payload→Finish exec；先跑无副作用 fixture，再按攻击路径门禁逐项关闭。每批更新 UML、所有权/生命周期表、测试和记录。
5. Host：定向测试、testDebugUnitTest、assembleDebug、assembleDebugAndroidTest、上游源 SHA 对拍、APK/ELF ABI/entrypoint 与 KO patch/hash 校验。
6. 设备：AVD emulator-5554 与 A301SO 均在线（本轮 adb devices -l）。先在 AVD 验证 UI 和无副作用 fixture；再以 A301SO 验证 APK 身份、launcher uid/context、worker uid/context、文件交接、log 与 staging。未经单独真机门禁不运行生产漏洞 ELF；不以 AVD/fixture 宣称 supported。

## 6. 当前阻断与证据

- 用户裁定与 Shizuku/Finish 语义见 §2；范围许可不等于生产实现批准。rev11a review1 判 BLOCKED。
- DFRoot provenance：pinned v4.7 commit 49e992ef3c9f7ec03c8efea6e3cf552f7ec6a347；dfroot.c SHA-256 fc1cdb216b3364bd408acd76bb7943bcb6f5f257ec134962df96ac97b6b460be，bootstrap.c SHA-256 5b19bfe9d4c632794d52de71a513d1bac2820e0fbd474ce19813913782111ce9；Glued exp.c 与 pinned 原文一致。当前 CMake 是适配版；旧 SOURCE-MANIFEST 的 CMake/exp 哈希不匹配 v4.7，df_log.c/.h 也非该 commit。保留旧 manifest，增建 pristine manifest 与上游来源副本。
- KO 来源证据：8 个当前 KO 文件与 `/Users/nickji/Documents/Program/Analysis/DFRoot` v4.6 checkout 的忽略构建产物逐字节一致；上游 `lkm/Makefile` 与 `.github/workflows/build.yml` 显示 KMI matrix 和 DDK image `20260828`。尚未从可复核的 pinned workflow artifact 对拍这些 KO，也未为新增 KO patch 生成替代字节及逐变体 hash。
- 当前源码：43284glued/CMakeLists.txt 将上游 bootstrap.c 编成 libbootstrap.so；libcxx.S insmod argv 无 module 参数；exp.c 仅接 IPsec 参数并改写 KO 路径；cleanup() 忽略恢复返回值；BackendRunUiState 无 handoff 状态，RunPage 将 FlowResult.Ok 显示为成功。AVD 包属性不证明 UMH/SELinux/文件交接可用。
- 43499：方案顺序已闭合为 common RootManager 单次 direct-path late-load；实际 libksud 身份/ABI、双 launcher 可达性、FolkPatch readiness 与失败转移仍是实现/设备门。
- AVD 当前 UI/selector 与 2.0.12 APK、profile source、已有 safe fixture 结果见归档详版与 docs/exec-glue/ 下对应 transaction；最近 adb devices -l 见 emulator-5554 online；未运行生产 Glued ELF。
- rev8/rev9 critic 是历史阻断，不覆盖 rev11：参数/UMH ABI、SELinux/launcher 可达性、Flow 状态、cleanup 与 KO provenance 尚未闭合。
- review1 冻结快照 1dc5ad14… 判 BLOCKED（3 阻断、3 重要、1 建议）；本版补 marker schema 与 /dev/dfm6→ROOT_READY 契约。目录访问、逐 KMI ABI/产物和 marker 防重放仍是门；未运行生产 Glued ELF。
- 2026-10-29 M0 设备门前置阻断：A301SO `uname -r=5.15.189-android13-8-00016-g51bba4309aac-ab14546557`；新建 M0 KO 与原 embedded KO 的 `vermagic=5.15.202-android13-5.15.202_r00-dirty SMP preempt mod_unload modversions aarch64` 不匹配。依计划 stop condition，未安装 M0 APK，未运行 M0 ELF/KO；设备仍为 Enforcing，已安装 APK 与门禁前备份逐字节相同。重建前须获得与设备精确 KMI/kernel build 对应的 DDK/KO，并复核 vermagic、config、modversions、架构；详见 `docs/analysis/device-gates/20261029-a301so-43284-m0-marker-gate.md`。design-critic 总判：M0 BLOCKED，非 marker 协议失败；不批准生产实现。
- Follow-up 2026-10-29: target-.189 KO passed host vermagic/config/10 common-ABI CRC checks; App marker test failed before KO insertion/UMH at `read_vendor 0x1ac70` and produced no ROOT_READY/RELEASE_FINISH. App/source/default ELF rollback passed. The user-approved recovery reboot restored active crash_dump64 byte-for-byte to the pristine APEX payload; installed APK remains the pre-gate 2.0.12 build. M0 remains failed and was not rerun; production implementation remains blocked. Details: `docs/analysis/device-gates/20261029-a301so-43284-m0-marker-gate.md`.
- 2026-10-29 root-cause correction: the earlier `.202` vs `.189` vermagic stop was over-strict and did not explain the later failed attempt; the `.189` candidate passed host identity checks and execution reached `patch_ko`. The measured carrier `/vendor/lib64/libbinderdebug.so` is 109,688 B, while the unstripped M0 KO is 256,240 B. The failing read was at `0x1ac70` (=109,680), leaving only 8 B for a 16-byte read; therefore the attempt stopped before KO insertion/UMH because the KO did not fit its carrier, not because of the version string. The latest approved reboot verified boot=1, kernel `.189`, Enforcing, and pristine crash_dump64 SHA `15288efc30e073dbb29a4b357d70e8b33c8136fa442c2def91a599fef85de066`.

## 7. M0 carrier-capacity repair batch (marker-only)

- Scope: modify only `app/src/main/cpp/43284glued/m0/lkm/Makefile`, `app/src/main/cpp/43284glued/m0/exp.c`, and `app/src/androidTest/kotlin/com/ghostlock/app/backend/cve43284glued/M0MarkerGateTest.kt` for the isolated M0 fixture. Keep production 43284 sources, Flow, Shizuku route, Payload/Finish, and all other backends untouched. Exercise only the existing App `M0MarkerGateTest#appLauncherCompletesRootReadyAndSingleReleaseMarkers`.
- Change A: keep the existing M0 objcopy strip and explicitly apply `llvm-objcopy --strip-debug`. Verified exact `.189` KO is ELF64/AArch64, 12,512 B, SHA `4f56858dd02ec53f14f2f3ca1e9d8fd48f83be85d6f32eaecc339220b54e5c79`, no `.debug_*`; 704-byte `__versions` equals the audited target candidate. Allocated code/data sections match except build-id/note metadata; all 71 normalized relocations match by offset/type/symbol/addend; ten imported CRCs match.
- KMI artifact: the normal Gradle `buildM0MarkerKo` path uses the generic DDK KDIR and emitted a 11,776 B KO with no `__versions`; it is not valid for this device gate and must not be packaged. The fresh target KO comes from the isolated copy of M0 LKM source built against Podman volume `a301so-kernel-common-189` (`/source/kernel-clean`, `/source/out`) and the pinned image. Record config/toolchain/source hashes, output hash, `.189` vermagic, 704-byte `__versions` equality and imported CRCs.
- Packaging: place only the verified fresh KO at `build/app/m0/fixture/43284glued/ko/dfroot-android13-5.15.ko`; record and check its hash before assembly. Invoke Gradle with `-Pm0Marker=true -x :app:buildM0MarkerKo` so the generic task cannot overwrite it; reject missing/stale fixture bytes, and compare the final APK entry hash/size to the KO input. All overrides stay under `build/`; no Gradle/product source change.
- Change B: add fail-closed `stat()` capacity preflight over the selected KO's 16-byte-padded length and selected regular-file carrier size. Call it in `exploit()` before `patch_helper()`; a stat error, malformed KO size, or undersized target must exit without any page-cache mutation. Emit `M0_PREFLIGHT_OK` or `M0_PREFLIGHT_FAIL` as explicit evidence.
- Host gate: baseline `./gradlew :app:testDebugUnitTest --no-daemon --console=plain` → `BUILD SUCCESSFUL in 39s`, exit 0. Final `./gradlew :app:testDebugUnitTest :app:assembleDebug :app:assembleDebugAndroidTest -Pm0Marker=true -x :app:buildM0MarkerKo --no-daemon --console=plain` → `BUILD SUCCESSFUL in 41s`, exit 0; APK entry contains exact target KO at offset 517080; app version/signature match installed baseline.
- Device gate preflight had boot=1, same A301SO `.189` kernel, Enforcing, pristine crash_dump64 SHA, no KSU module, and original installed APK hash `37756ac1…`. M0/test APK install succeeded, but instrumentation did not enter the test: user was `RUNNING_LOCKED`, and ActivityManager logged `SecurityException: Package com.ghostlock.app is not encryption aware!`. No M0 ELF/KO ran; crash_dump64 remains pristine. Cleanup restored the pre-gate app APK byte-for-byte and uninstalled the test package. Wait for user unlock, reinstall the M0 fixture, then run only the App marker test and verify markers/worker/canaries. No Payload/Finish, Shizuku, production Flow, or production ELF.
- Current state: design-critic approved; fixture source edits, exact `.189` KO/KMI identity, APK entry/version/signature, host tests/build, and pristine device baseline are verified. Device instrumentation was blocked before test entry by `RUNNING_LOCKED`; fixture APK/test package are restored, crash_dump64 remains pristine, and user unlock is pending before reinstall/retry. Production implementation remains blocked.
