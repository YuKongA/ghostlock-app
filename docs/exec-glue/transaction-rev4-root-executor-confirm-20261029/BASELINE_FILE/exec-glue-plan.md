# Exec Glue 部署计划（唯一活动版）

> 状态：rev4 设计待完成；生产代码门未开放。目标版本：2.0.12。
> 取代关系：本文件是唯一活动计划；此前批次记录、设备证据与详版 rev1–rev4 见归档 docs/archive/20261029-1025-exec-glue-plan-history.md。
> 依据：docs/development/软件工程守则.md、design-philosophy.md、engineering-standards.md、design-review-principles.md。

## 1. 目标与边界

在 dev-nocpp 工作树中交付同级 Glued 后端 43499-Glued 与 43284Glued。App 的 SequentialFlow 各只有一个 exec step；Shizuku 是可选 launcher：授权且可达时只选 Shizuku，否则选 App；已选 launcher 的启动结果不确定时不得再启动第二个 child。现有 UI 页面和控件保留，原版 CVE 后端与 Custom Lua 灰显不可选，原版 native targets 不构建。上游 43499 C/C++ 与 43284 C/KO 字节保持不变；不 commit/push。

本次只处理共用 Payload/Finish 接线、runDir/terminal 生命周期与部署证据，不改攻击原语、profile schema 或既有 UI 布局。每个生产批次先经设计 gate，再单批实现和验证。

## 2. 已确认的用户决策

- Safe mode 开关生成本次运行唯一 effective profile；43499 GLK1 meta.safe_mode 与 root script 使用同一值。
- Root Manager 不增加 Glued 专属候选搜索/优先级；走 common Payload/Finish。
- 43284 保持上游 C/KO 原文，采用独立 root Finish handoff。
- Shizuku 不是必需；有授权走 Shizuku，否则走 App launcher。

## 3. rev4 设计合同

### 43499-Glued

Profile 先解析，再按 Safe mode 开关派生 effective profile；源 conf 不改。GLK1 与脚本同值。Flow 为每次 run 生成固定 allowlist 文件和 UUID runDir；root script 预置于 native 预期路径，作为 regular 0444 文件。native O_TRUNC 写失败后仍由 rooted victim 按同路径只读打开并 exec；该行为须用无副作用 fixture 验证 inode、owner、mode 和内容 hash。

脚本镜像 ops.cpp:236-431 的 root gate、iomem/debug、policy fixup、KMI、safe-mode、module wait、trap 和失败状态。KSUD 搜索/fallback 从 Glue 路径移除。已选 RootManagerSpec 的单一 package 由 PackageManager 得到 nativeLibraryDir/libksud.so；执行前核验绝对路径、regular file、可执行位、目标 ABI，以及 App/Shizuku 两个 victim 执行域的可达性。任一校验失败即 fail-closed；不枚举候选、不使用其它路径 fallback。FolkPatch 只走 common launch-only/LKM 语义，所需 apd/LKM 条件不成立时 readiness fail-closed。

执行顺序固定为：profile/root gate → SELinux policy fixup → common RootManager direct-path late-load（恰一次）→ KernelSU module wait/restore → 其它 common payload/finish → terminal marker。任一 prerequisite 失败即跳过后续阶段并保留具名 status；Finish tail 不重复 RootManager request。上游 C++ 源文件不改。

### 43284Glued

Flow 将 common finish.sh 与 payload 写入 launcher 自有 runDir。上游 ELF/DFRoot bootstrap 完成并达到 direct-child terminal 后，先关闭本次 IpSecRuntime，再独立执行 RootFinishHandoff；DFRoot 原有 late-load 不重复。root Finish 脚本先核验 uid=0，再执行 non-RootManager payload/finish，并写带 runId/status 的 terminal marker。不能把 ELF exit=0 当成 Finish 成功。

RootFinishHandoff 使用新 backend-specific adapter；不得复用已 Deprecated 的 FinishExecution。用户已确认按所选 RootManager 的固定 su -c 接口执行。冻结映射为当前每个 RootManagerSpec 均声明固定 command token su 与 argv 前缀 [su, -c]；RootManager 仅提供授权/批准，不触发候选路径扫描。ProcessBuilder 只启动一次；-c 的单一命令参数只含 /system/bin/sh 与经过字符集校验、shell-quoted 的固定 runDir/finish.sh 路径，脚本内容与 payload 不拼入 argv。新脚本第一步校验 id -u == 0 并写带 runId/status 的 terminal marker；su 缺失/授权拒绝、非 root、启动失败或非零退出均 fail-closed，保留 runDir/log。若某 RootManagerSpec 的 su -c 不可用，该 manager 在 Glued43284 readiness 中 fail-closed。

### 共用终态与资源

成功需要 ELF exit=0、IpSec close=0（43284）、Finish script status=0、runId marker 匹配、log EOF 和文件身份复核全部成立。ELF 非零跳过 Finish 并为 Failed；IpSec close 失败为 Failed；marker/EOF/worker 状态不可观测、binder death 或取消为 Unknown 并保留 runDir。只有确认无未终结 child/worker 后才能 cleanup。App 与 Shizuku 不共享绝对路径；两端均独立 staging 并验证 regular/no-symlink、owner、mode、hash。

## 4. 允许修改范围

- 43499：profile codec/Flow、backend-specific root-script generator、common FinishScript/PayloadRequests strict-path 接口、run-file stager/process/host。
- 43284：Flow/host、全新 Glued43284RootFinishHandoff 与对应 launcher adapter；FinishConfig.RootManagerSpec 增加固定 su command token/argv 声明。
- 对应 JVM/Android tests、docs/development/full-process-uml.md 的 exec/Finish sequence。
- 保留：UI 布局、原版 backend 实现、所有上游 main/cpp 文件、CMake 源 target 决策、LegacyProfileConverter 转换语义、已有 LKM Finish 路径。

## 5. 设计与实现门

1. 用户已确认固定 su -c 接口；计划冻结现有每个 RootManagerSpec 使用 token su、前缀 [su, -c]，无 path search/fallback。design-critic 需确认映射、argv shell quoting、单次 handoff、uid=0 gate、marker 与拒绝语义；运行时 su 缺失/授权拒绝即 fail-closed。
2. design-critic 复核最终压缩计划；阻断项为零且用户批准后才改生产源码。A3 已压缩达标；43499 direct-path 仍须实现并证明 ABI/权限/SELinux 可达。
3. 分批实施：先 43499 bridge，关闭单 late-load、script parity、staging identity 与 detached terminal 门；再 43284 root handoff，关闭 IpSec/status/marker/cancel 门。每批更新 UML、测试和记录，不合并跳门。
4. host：针对性测试、完整 testDebugUnitTest、assembleDebug、assembleDebugAndroidTest、上游源 SHA 对拍、APK/ELF ABI/entrypoint 校验。
5. 设备：当前 adb devices 只见 AVD emulator-5554；A301SO 当前不可见。先在 AVD 验证 UI 与无副作用 launcher/Finish fixture；A301SO 重新连接后验证 APK 身份、launcher uid/context、root script handoff 与日志。未经专门真机门禁不运行生产漏洞 ELF；不以 AVD/fixture 结果宣称 production exploit supported。

## 6. 当前阻断与证据

- 用户已确认：43284 root Finish 使用所选 RootManager 的固定 su -c 接口；缺失/拒绝 fail-closed。活动计划当前冻结为 token su、前缀 [su, -c]；需 design-critic 复核后再进入用户批准 gate。
- 43499：方案顺序已闭合为 common RootManager 单次 direct-path late-load；实际 libksud 身份/ABI、双 launcher 可达性、FolkPatch readiness 与失败转移仍是实现/设备门。
- AVD 当前 UI/selector 与 2.0.12 APK、profile source、已有 safe fixture 结果见上述归档详版与 docs/exec-glue/ 下对应 transaction。最近 adb devices -l：emulator-5554 online；未运行生产 Glued ELF。
- 设计复核记录：2026-10-29 · 输入计划 SHA-256 c4c244a03850577da54b48c61a5697dcb76a8452947bb83af59d9843ae0b980d · A3 通过（6603 B）；用户已关闭 executor 接口确认项。最终 adapter 映射与 shell quoting 待本轮 design-critic 复核。43499 direct-path、脚本 parity 与真机验证均未完成。
