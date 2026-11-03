# Exec Glue 部署计划（唯一活动版）

> 状态：rev6 design-critic 复核阻断（3 阻断、3 重要、1 建议）；用户已允许为 43284 root handoff 修改上游 C/KO，但生产代码门关闭。目标版本：2.0.12。
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

## 3. rev6 设计合同

### 43499-Glued

Profile 先解析，再按 Safe mode 开关派生 effective profile；源 conf 不改，GLK1 与脚本使用同一值。Flow 为每次 run 生成固定 allowlist 文件和 UUID runDir；root script 预置于 native 预期路径，作为 regular 0444 文件。native O_TRUNC 写失败后仍由 rooted victim 按同路径只读打开并 exec；用无副作用 fixture 验证 inode、owner、mode 和内容 hash。

脚本镜像 ops.cpp:236-431 的 root gate、iomem/debug、policy fixup、KMI、safe-mode、module wait、trap 和失败状态。KSUD 搜索/fallback 从 Glue 路径移除。已选 RootManagerSpec 的单一 package 由 PackageManager 得到 nativeLibraryDir/libksud.so；执行前核验绝对路径、regular file、可执行位、目标 ABI，以及 App/Shizuku 两个 victim 执行域可达性。校验失败即 fail-closed；不枚举候选、不 fallback。FolkPatch 只走 common launch-only/LKM 语义，所需 apd/LKM 条件不成立时 readiness fail-closed。

执行顺序：profile/root gate → SELinux policy fixup → common RootManager direct-path late-load（恰一次）→ KernelSU module wait/restore → 其它 payload 与状态恢复 → common Finish（最后业务动作）。管理器 binary 只承担显式 late-load，不承担 Payload/Finish 提权。若 Finish 启用软重启，重启请求是脚本最后命令；之后无 marker/log/cleanup/其它工作。Finish 输出目标在启动前打开并落盘；执行后只被动观察自然退出与已知 child/worker/FD，不 drain/write。软重启导致观察中断记 Unknown，不补写成功状态。上游 C++ 不改。

### 43284Glued

Flow 仍只有一个 SequentialFlow exec step；App/Shizuku 只负责启动上游 ELF，不作为 root 来源。当前 libcxx.S 入口先检查 uid/tid，再通过 insmod 进入选中的 DFRoot KO；KO 中的 usermode-helper 是 root handoff 候选。现有 bootstrap.c 只读 DFRoot prefs、调用 ksud late-load，并可自行触发 soft reboot；没有 common Payload/Finish 请求接口。现有 exp.c 把 /dev/dfm6 当成功后返回，随后 main() 调用 cleanup()；Finish 前必须完成 native hook 恢复。

**rev6 root-handoff 候选设计：** 将 DFRoot 实际 root usermode-helper 回调改为 Glued 的单次 root worker。回调继续前须自证 getuid()/geteuid()==0，并检查设备上实测、列入 allowlist 的 SELinux domain；launcher 验证 socket peer credentials 的 uid=0。App/Shizuku launcher 在启动 ELF 前创建单活动会话的本机 Unix-domain rendezvous；root worker 通过固定版本帧交换 runId、有限大小的 payload/finish 内容与 SHA-256。协议不接收脚本路径或 shell 命令插值，不以 manager su 提权。错帧、拒绝、断连、超时、重复连接或身份不符均 fail-closed，不启动第二个 child。AF_UNIX/SELinux 可达性须分别有 App 与 Shizuku 两域的设备证据。

root worker 按固定文件名将经 hash 验证的脚本写入唯一 runDir，不从输入构造路径。顺序：root callback ready 与既有 ksud late-load → exp.c 观察 ready 并完成 cleanup → launcher 等待 ELF 自然 exit=0 → 关闭本次 IpSecRuntime → 发送 RELEASE_FINISH 与脚本数据 → root worker staging、预打开 log → common Payload → 成功后 execve 替换为 common Finish。Finish 是 root 进程最后执行体；软重启请求位于脚本末尾。C/bootstrap 在交出 Finish 后不写 marker/log、不 cleanup、不运行 reboot fallback 或其它命令。launcher 只被动观察预先建立的 pidfd/连接退出；软重启或观察中断为 Unknown，不追加成功 marker/log。若无法在不做 Finish 后动作的条件下证明退出结果，UI 只报告已发出 Finish handoff，不声称 Finish 成功。

**必须在实现前闭合的装载与文件边界：** 当前 CMake 输出 libbootstrap.so，但现有 host allowlist 仅含 finish.sh 与 payload；KO 固定 helper path 与 APK staging、Unix socket 地址之间尚未接通。design-critic 须逐项给出并评审 root helper 的实际装载位置/启动 ABI、文件标签与执行权限、App/Shizuku socket 可达性、runDir/log 可读方案、pidfd/worker/FD 观察与失败回收。若需改 KO，逐个 Android/kernel 变体保留原 SHA、精确 patch/hash 和替代字节验证；若只需改 C，保留 pristine upstream source 与 SOURCE-MANIFEST.json 原始哈希。当前 app copy 中 exp.c 和 CMakeLists.txt 已与 manifest hash 不同，df_log.c/df_log.h 缺失；后续先核对既有差异并补齐来源/归档，不覆盖现有工作。

### 执行结束与资源

Finish 是最后业务执行体，不存在“terminal 命令”或 Finish 后业务阶段。日志目标、脚本文件、fd、锁、IpSecRuntime 和 staging 检查均在释放 Finish 前完成。Finish stdout/stderr 直接写入预先打开的落盘目标；launcher Finish 后不 drain/write。root worker 仅在 Finish 前发送 handoff-ready；之后只允许系统被动观察现有进程/FD 自然退出，不启动 worker、不写状态。

runDir/log 由本次流程保留；下一次运行的独立 preflight 才可回收旧目录。回收前按 runId 核对 direct child、detached worker、输出句柄均已结束，并复核目录/文件身份；只凭年龄、状态或 marker 不得删除。不可观测、身份不符或仍存活的执行者均保留。Unknown 目录不得删除；容量/回收 preflight 必须早于 runDir 创建、staging 与 launcher 启动；同一 launcher root 的并发 preflight 串行化，无法取得唯一处理权则 fail-closed。配额超限时拒绝新 run、返回具名原因并提示人工处理，不创建 runDir，也不自动删除 Unknown/活跃目录。43284 的 IpSecRuntime 必须在 root worker 接收 RELEASE_FINISH 前关闭。App 与 Shizuku 不共享绝对路径；两端独立 staging 并验证 regular/no-symlink、owner、mode、hash。

## 4. 允许修改范围

- 43499：profile codec/Flow、backend-specific root-script generator、common FinishScript/PayloadRequests strict-path 接口、run-file stager/process/host。
- 43284：允许最小改动 exp.c、bootstrap.c、必要的 libcxx.S，以及经评审确认确需的 KO 变体；同时允许对应 CMake、source manifest、Flow/host/stager/root script bridge。具体文件与 ABI 须由 rev6 design-critic 逐项收敛。
- 对应 JVM/Android tests、docs/development/full-process-uml.md 的 exec/Finish sequence 与所有权/生命周期图。
- 保留：UI 布局、原版 backend 实现、43499 上游 C/C++、LegacyProfileConverter 转换语义、已有 LKM Finish 路径；43284 pristine upstream 来源与原始 manifest 哈希始终保留，Glued 改动不得覆盖来源副本。

## 5. 设计与实现门

1. rev4 的 su -c/Finish 后 marker 方案作废。rev6 critic 复核 root UMH helper 的确切调用链与装载路径、Unix-domain rendezvous 协议、euid/SELinux 证据、App/Shizuku 两域可达性、拒绝/断连/超时/重试与单次执行。APK 内置 su 不属于设计。
2. Finish 为最后业务动作：脚本无 Finish 后命令/marker/log/cleanup，软重启为 Finish 内最后命令，日志目标执行前建立，Finish 后无 launcher drain/write。只作被动进程观察；软重启或未观察到退出记 Unknown；旧 runDir 只在下一 run preflight 核验 runId、child/worker 与文件身份后回收；配额超限拒绝新 run并提示人工处理。
3. rev6 design-critic 的 helper 装载链、socket/SELinux 可达性、upstream/hash 三个阻断关闭并复核无阻断后，仍须用户明确批准才可开始生产源码实现。计划 ≤16 KiB；43499 direct-path 与脚本 parity 是独立实现门。
4. 分批实施：先 43499 root-victim bridge；43284 单独一批实现 root callback、launcher rendezvous、Payload→Finish exec handoff，先跑无副作用 fixture，再按攻击路径门禁逐项关闭。每批更新 UML、所有权/生命周期表、测试和记录，不跳门。
5. Host：针对性测试、完整 testDebugUnitTest、assembleDebug、assembleDebugAndroidTest、上游源 SHA 对拍、APK/ELF ABI/entrypoint 和 KO patch/hash 校验。
6. 设备：当前 AVD emulator-5554 online，A301SO 当前不可见。先在 AVD 验证 UI 与无副作用 launcher/Finish fixture；A301SO 重新连接后验证 APK 身份、launcher uid/context、root worker uid/context、socket、log 与脚本 staging。未经专门真机门禁不运行生产漏洞 ELF；不以 AVD/fixture 结果宣称 production exploit supported。

## 6. 当前阻断与证据

- 用户 2026-10-29 新确认：允许修改 43284 上游 C/KO 加 root handoff；common Payload/Finish 不依赖 manager su；Finish/soft-reboot 后不追加动作。该授权仅变更设计边界，不等同于生产实现批准。rev5.2 critic 结果不覆盖 rev6。
- 43284 源码证据：bootstrap.c:196–234 目前运行 ksud late-load 和可选 soft reboot；exp.c:557–599 将 dfm6 判成功并在返回后 cleanup；CMakeLists.txt 构建 libbootstrap.so；现有 Kotlin allowlist 只列 finish.sh/payload。KO usermode-helper 及 helper path 的实际可执行装载链仍待核实。
- 上游来源证据：SOURCE-MANIFEST.json 共 22 项，其中 19 项属于 43284；当前 Glued 目录 15 项哈希匹配，CMakeLists.txt 与 exp.c 有既有差异，df_log.c/df_log.h 缺失；src/upstream/43284 尚无来源目录，remote/main 当前也无该路径的 git tree。所有既有改动保留；恢复 pristine 来源并逐文件对拍是实现前置。
- 43499：方案顺序已闭合为 common RootManager 单次 direct-path late-load；实际 libksud 身份/ABI、双 launcher 可达性、FolkPatch readiness 与失败转移仍是实现/设备门。
- AVD 当前 UI/selector 与 2.0.12 APK、profile source、已有 safe fixture 结果见归档详版与 docs/exec-glue/ 下对应 transaction；最近 adb devices -l 见 emulator-5554 online；未运行生产 Glued ELF。
- 设计复核记录：rev5.2 快照 SHA-256 9f8e5aad4366ea04131fbd64d3617f813ae00c33db25749528f578b00aba0417 · 11,130 B · 仅适用于 rev5.2。rev6 复核输入 SHA-256 c80ac00604a0656bf7840a1fde0114ca251f9f1a1708d7ad1b8700fd5792094b · 12,319 B；总判不能进入 user-approval/implementation gate，3 阻断、3 重要、1 建议。阻断：root UMH/helper 装载位置/ABI/触发链未闭合；Unix socket owner/address/SELinux/peer-auth 状态机未冻结；pristine upstream/source manifest 与逐文件 patch/hash 未闭合。重要项：exp cleanup 与 rendezvous 资源所有权未证明；pidfd/EOF 被动完成观察未验证；43284 必须独立批次先 fixture。root helper、socket、worker/FD、KO ABI、生产 ELF、App/Shizuku 双域及真机均未验证。
- 2026-10-29 rev6：按用户决定开放 43284 C/KO root handoff 的最小修改边界；加入 root-worker rendezvous 与 Finish 最后 exec 的候选合同。design-critic 发现上述阻断；下一步先闭合文件来源/装载 ABI、精确 socket 状态机及所有权表，再提交新快照复核；不得开始生产实现。
