# Exec Glue 部署计划（唯一活动版）

> 状态：rev5.2 design-critic 复核通过；等待用户决定 43284 root authority 路线；生产代码门关闭。目标版本：2.0.12。
> 取代关系：本文件是唯一活动计划；此前批次记录、设备证据与详版 rev1–rev4 见归档 docs/archive/20261029-1025-exec-glue-plan-history.md。
> 依据：docs/development/软件工程守则.md、design-philosophy.md、engineering-standards.md、design-review-principles.md。

## 1. 目标与边界

在 dev-nocpp 工作树中交付同级 Glued 后端 43499-Glued 与 43284Glued。App 的 SequentialFlow 各只有一个 exec step；Shizuku 是可选 launcher：授权且可达时只选 Shizuku，否则选 App；已选 launcher 的启动结果不确定时不得再启动第二个 child。现有 UI 页面和控件保留，原版 CVE 后端与 Custom Lua 灰显不可选，原版 native targets 不构建。上游 43499 C/C++ 与 43284 C/KO 字节保持不变；不 commit/push。

本次先修订 root 执行权与 Finish 终止语义；不改攻击原语、profile schema 或既有 UI 布局，也不动生产代码。每个生产批次先经设计 gate，再单批实现和验证。

## 2. 已确认的用户决策

- Safe mode 开关生成本次运行唯一 effective profile；43499 GLK1 meta.safe_mode 与 root script 使用同一值。
- Root Manager 不增加 Glued 专属候选搜索/优先级；common Payload/Finish 不依赖管理器提供的 `su`。
- 43284 上游 C/KO 保持原文；Shizuku 不是必需，有授权走 Shizuku，否则走 App launcher。launcher 选择不等于 root 授权。
- Finish 可包含软重启；Finish 是本次业务流程的最后动作，之后不追加 marker、日志写入、清理或其它业务动作。

## 3. rev5.2 设计合同

### 43499-Glued

Profile 先解析，再按 Safe mode 开关派生 effective profile；源 conf 不改。GLK1 与脚本同值。Flow 为每次 run 生成固定 allowlist 文件和 UUID runDir；root script 预置于 native 预期路径，作为 regular 0444 文件。native O_TRUNC 写失败后仍由 rooted victim 按同路径只读打开并 exec；该行为须用无副作用 fixture 验证 inode、owner、mode 和内容 hash。

脚本镜像 ops.cpp:236-431 的 root gate、iomem/debug、policy fixup、KMI、safe-mode、module wait、trap 和失败状态。KSUD 搜索/fallback 从 Glue 路径移除。已选 RootManagerSpec 的单一 package 由 PackageManager 得到 nativeLibraryDir/libksud.so；执行前核验绝对路径、regular file、可执行位、目标 ABI，以及 App/Shizuku 两个 victim 执行域的可达性。任一校验失败即 fail-closed；不枚举候选、不使用其它路径 fallback。FolkPatch 只走 common launch-only/LKM 语义，所需 apd/LKM 条件不成立时 readiness fail-closed。

执行顺序固定为：profile/root gate → SELinux policy fixup → common RootManager direct-path late-load（恰一次）→ KernelSU module wait/restore → 其它 payload 与状态恢复 → common Finish（最后业务动作）。管理器 binary 只承担显式 late-load 业务，不承担 Payload/Finish 的提权。若 Finish 启用软重启，重启请求是脚本最后一条命令；之后不写 marker/日志、不清理、不启动其它工作。Finish 输出目标须在启动前打开并落盘；执行后仅被动等待自然退出、只读核对已知 child/worker 与输出句柄关闭，不 drain/write。只有 exit=0 且已知执行树结束才报告成功；软重启导致观察中断记 Unknown，不补写“成功”状态。上游 C++ 源文件不改。

### 43284Glued

Flow 将 upstream ELF 的参数和所需文件写入 launcher 自有 runDir。ELF child 自然退出后释放本次 IpSecRuntime；但 App launcher 是 app UID，Shizuku UserService 通常是 shell UID，二者都不能据此执行 root Finish。上游 `bootstrap.c` 自己调用 DFRoot 的 ksud late-load，却没有外部 Finish path/callback，也不把 root 执行上下文交回 launcher；因此 ELF exit=0 不能证明 post-exit root Finish 可执行。43284 的 RootManagerPayload 保持 no-op，避免重复 manager bootstrap。

**43284 root-authority 阻断（未解决，不得实现原 rev4 的 su handoff）：** common Payload/Finish 必须拿到真实系统 root 执行上下文；不能把 app/shell UID 当 root。禁止依赖管理器 `su`，也不接受“把一个 `su` 文件放进 APK”作为提权方案：APK 内可执行文件仍继承启动者 UID；要变成 root 需要既存的 root broker、受信安装/系统镜像特权或其它经验证的 root 授权通道。当前上游 C/KO 不暴露可调用的 root Finish broker，故在“C/KO 不改 + 不用管理器 su”两项约束下，未找到已证实的 post-exit root handoff。只有选定并验证一个非 `su` 的 root-authority 接口后，才能打开 43284 Finish 实现门；若没有这种接口，必须先改变其中一项约束或暂缓 43284 common Finish。

rev4 的 `RootFinishHandoff + su -c + terminal marker` 设计已被用户本轮纠正并作废，不是当前实施合同。任何替代实现必须先证明：执行者的 euid=0 与 SELinux 域、root authority 来源、App/Shizuku launcher 可达性、拒绝/断连语义及单次执行。若改用 RootManager 的非 `su` 服务接口，必须逐 manager 声明真实 API 和授权语义；不能虚构通用接口。

### 执行结束与资源

“子进程退出状态”只描述当前执行 child 自然退出/取消的观测，不是额外命令。资源释放、校验和日志目标准备都在最后 Finish 前完成；Finish 后不追加业务动作、marker、日志写入或 cleanup。Finish 的 stdout/stderr 必须在启动前接到已落盘日志目标；Finish 后 launcher 只被动观察已启动 child 的退出，不再 drain 并写日志。观察到自然 exit=0 才报告完成；软重启、binder death、取消或未观测到退出均为 Unknown，不宣称成功。runDir/log 由本次流程保留；下一次运行的独立 preflight 才可回收旧目录。回收前按 runId 核对关联 direct child、detached worker、输出句柄均已结束，并复核目录/文件身份；只凭年龄、旧状态或 marker 不得删除。任何不可观测、身份不符或仍存活的执行者都保留目录。Unknown 目录不得删除；容量/回收 preflight 必须早于新 runDir 创建、staging 与 launcher 启动；同一 launcher root 的并发 preflight 串行化，无法取得唯一处理权则 fail-closed。配额超限时 fail-closed 拒绝新 run、返回具名原因并提示人工处理，不创建 runDir，也不自动删除 Unknown/活跃目录。43284 的 IpSecRuntime 必须在 Finish handoff 之前关闭。App 与 Shizuku 不共享绝对路径；两端独立 staging 并验证 regular/no-symlink、owner、mode、hash。

## 4. 允许修改范围

- 43499：profile codec/Flow、backend-specific root-script generator、common FinishScript/PayloadRequests strict-path 接口、run-file stager/process/host。
- 43284：在 root-authority 设计门关闭前不实现 post-exit Finish handoff；通过门后只允许按已评审的非 `su` 接口列出 Flow/host/adapter 改动。
- 对应 JVM/Android tests、docs/development/full-process-uml.md 的 exec/Finish sequence。
- 保留：UI 布局、原版 backend 实现、所有上游 main/cpp 文件、CMake 源 target 决策、LegacyProfileConverter 转换语义、已有 LKM Finish 路径。

## 5. 设计与实现门

1. rev4 固定 `su -c`/marker 方案作废。新设计复核必须先确定 43284 非 `su` root authority 或明确缩小 43284 Finish 目标；验证 euid/SELinux、app 与 Shizuku 两域、拒绝/断连与单次执行。APK 内置 `su` 必须证明独立提权来源，否则拒绝该设计。
2. 明确 Finish 为最后业务动作：断言生成脚本没有 Finish 后命令/marker/log/cleanup；软重启为最后命令；日志目标必须在执行前建立，Finish 后无 launcher drain/write。只有被动观察到自然 exit=0、已知 child/worker 结束且输出句柄关闭才报告 Finish 完成，重启中断归 Unknown；旧 runDir 仅在下一 run preflight 按 runId 核对 child/worker 与文件身份后回收，Unknown/不可观测/身份不符均保留；配额超限拒绝新 run并提示人工处理。
3. design-critic 复核 rev5.2 后，用户批准才可改生产源码。A3 仍须 ≤16 KiB；43499 direct-path 与脚本 parity 仍是独立实现门。
4. 分批实施：先 43499 root-victim bridge，关闭单 late-load、script parity、staging identity 与自然退出/执行树结束门；43284 只有 root-authority 设计门闭合并重新批准后才实施。每批更新 UML、测试和记录，不合并跳门。
5. host：针对性测试、完整 testDebugUnitTest、assembleDebug、assembleDebugAndroidTest、上游源 SHA 对拍、APK/ELF ABI/entrypoint 校验。
6. 设备：当前 adb devices 只见 AVD emulator-5554；A301SO 当前不可见。先在 AVD 验证 UI 与无副作用 launcher/Finish fixture；A301SO 重新连接后验证 APK 身份、launcher uid/context、root authority 与日志。未经专门真机门禁不运行生产漏洞 ELF；不以 AVD/fixture 结果宣称 production exploit supported。

## 6. 当前阻断与证据

- 用户本轮明确否定管理器 `su`，并指出 Finish/soft-reboot 后不应继续动作；rev4 对应 review 与等待实现批准状态全部作废。rev5.2 design-critic 复核无新增阻断，可请求用户决定是否按非 `su` RootManager API 继续 43284，或暂缓其 common Finish；43284 root-authority 仍是实施阻断，生产实现未获批准。
- 43499：方案顺序已闭合为 common RootManager 单次 direct-path late-load；实际 libksud 身份/ABI、双 launcher 可达性、FolkPatch readiness 与失败转移仍是实现/设备门。
- AVD 当前 UI/selector 与 2.0.12 APK、profile source、已有 safe fixture 结果见上述归档详版与 docs/exec-glue/ 下对应 transaction。最近 adb devices -l：emulator-5554 online；未运行生产 Glued ELF。
- 设计复核记录：2026-10-29 · 输入计划 SHA-256 a6f8946935f19801884f4accbd9b5372c7de66c26cccb7927386716a3f9633da · 总判“可进入 user-approval gate”· A3 通过（7631 B）；用户已关闭 executor 确认项。复核建议已补入 marker 顺序、43284 RootManager no-op 与命令计数测试。43499 direct-path、脚本 parity 与真机验证均未完成。
- 最终复核记录（rev4）：2026-10-29 · 输入计划 SHA-256 4fad3678a614c8cd2eda3c391fc7250b03fe83937d7be8ee7c04799914d9a219 · 7965 B · 仅适用于现已作废的 su/marker 设计；不得作为 rev5 批准依据。
- 2026-10-29 rev5 修订：按用户纠正移除管理器 `su -c` 与 Finish 后 terminal marker；解释子进程退出状态，明确软重启最后动作。design-critic 复核与用户批准均待完成。
- 2026-10-29 rev5.1：按复核补充 Finish 输出预打开与被动 wait、runId child/worker/FD 身份核对、Unknown 不删及配额超限拒绝新 run。
- 2026-10-29 rev5.2：将 child/worker/FD 检查要求落为可测试观测接口；容量 preflight 前置到创建 runDir/staging/launcher，拒绝需具名返回，并覆盖并发 preflight。
- design-critic 最终复核：2026-10-29 · 复核快照 SHA-256 `9f8e5aad4366ea04131fbd64d3617f813ae00c33db25749528f578b00aba0417` · 11,130 B · 无新增阻断，可进入用户批准 gate；源码实现、worker/FD 观测、并发 preflight、真实 root broker 与真机门禁未验证。该判定不是生产实现批准。
