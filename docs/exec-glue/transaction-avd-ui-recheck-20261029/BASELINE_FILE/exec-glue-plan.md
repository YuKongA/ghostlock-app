# Exec Glue 临时后端计划

## 目标

本工作树基于 `dev-nocpp`（2.0.12），新增临时后端 `43499-Glued` 与 `43284Glued`。两者均由新 app/Shizuku 启动器通过 `exec` 启动独立可执行文件；不把上游 C 代码改造成 JNI。

## 范围

- 上游源代码保存在 `src/upstream/`，以 manifest 校验完整性。
- Flow 只保留一个 exec step；通用日志、payload 与 finish 脚本写入运行目录并由可执行文件/finish 链读取。
- 43499 配置由 backend card 从内置 profile/conf 列表选择并写入运行目录。
- 43284 使用运行时参数文件，不引入新的 profile 格式。
- 既有 43499、43284、98260 原版后端在 UI 中显示为禁用，CMake 暂时不构建原版 target。

## 实施顺序

1. 建立 exec 后端身份、Flow 入口和 Shizuku AIDL 参数。
2. 接入可执行文件打包与 nativeLibraryDir 启动。
3. 接入 43499 profile 选择、payload/finish 脚本和日志目录。
4. 禁用原版 UI/native target。
5. 运行 Kotlin 编译、原版源完整性校验、exec ELF 检查和 APK 内容校验。

## 约束

本批不修改 `src/upstream/` 内上游 C 文件；任何适配仅通过 wrapper、参数文件、启动器和脚本完成。

## 纠偏记录（2026-10-28）

- Material 与 Miuix 选择器都显示全部 backend；只有 `43499-Glued` 与 `43284Glued` 可选，原版 43499/43284/98260 和动态 Lua 在菜单项内灰显且不可选，运行 Gate 与会话恢复同样拒绝 disabled spec。
- 两个 Glued backend 各自直接声明 `BackendSpec`；通用可执行进程实现位于 `backend/core/ExecutableBackendProcess.kt`。
- 43499 card 从上游 `kernel_profiles/index.conf` 选择内置 `.conf`，或通过 SAF 导入自包含 `.conf`。输入经 HOCON 解析、`LegacyProfileConverter`、`ProfileResolver` 校验后由 `Profile` 编码为 GLK1；Flow 以原版 `--ghostlock-app-call` 将 GLK1 二进制写入 stdin，不再把文本 conf 当成 stdin。
- 运行前就绪性以 profile 已选择且成功校验为准；空/损坏/失效配置 fail-closed。
- 基线快照（计划初始状态，已被下方 2026-10-29 launcher 批次更新）：当时主机 Kotlin 编译不等于 Shizuku exec 接线或真机部署；执行器仍为 App `ProcessBuilder`、43284 argv 为空。后续是否已完成必须以新 batch record 和设备/测试证据为准，不沿用此旧快照作为当前状态。

## 选择器与配置纠偏（2026-10-29）

- 根因：旧 `ExecGlueSpec` 把 43499 的 `titleRes` 和 `Ready` 继承给两个 Glued backend，导致 43284 也显示成 43499；旧 `BackendSelection` 不校验 disabled 状态，因而旧 43499、两个 Glued backend 和 43284 均可选。现已移除该抽象。
- 选择权收敛到 `Backends.isSelectable(spec)`：通过 `===` 只接受 `BackendCve43499Glued` 与 `BackendCve43284Glued` 两个 singleton，且二者显式声明 `selectable=true`。不依赖 `equals/hashCode`，避免动态 Lua spec 冒充内置对象。Material、Miuix、PathViewModel、会话恢复与 BackendRunGate 共用该判据；原版 43499/43284/98260、内置 Custom Lua 与动态 Lua 即使误声明 `true` 仍保持灰显/不可选。
- `43499-Glued` 的 backend readiness 只依赖 card 选择并验证通过的 profile；运行入口不会再把原版 JNI 43499 的“物理输入未接入”原因冒充为 Glued 的配置要求。profile card 从 `kernel_profiles/index.conf` 提供内置列表并支持 SAF 导入 `.conf`。
- 目标 profile 索引、profile 资产和 `LegacyProfileConverter.kt` 均核对为当前 `origin/main`（`437b3b257462e17d395cfd2cd8fb81536d6c6358`）的逐字节副本；当前工作树中旧 `app/src/main/assets/profile/` 有 146 个删除项，主资产配置唯一来源为 `kernel_profiles/`。debug source set 的实验配置仍独立保留，Glued 43499 不读取它。
- 代码库中仅保留两个 backend 的直接声明与 `ExecutableBackendProcess` 公共执行助手，不另设 Glued 继承层。

## 最终部署批次设计 rev1（已被 rev2 取代；2026-10-28）

### 现状与目标

当前工作树已具备双 launcher 执行骨架。本批路由规则为：每次运行在创建子进程前检查 Shizuku binder 与授权；两者都可用时只选 Shizuku UserService，否则只选 App 自身进程权限启动。若选定 launcher 的启动/连接结果不确定，不尝试另一个 launcher，以免产生重复 ELF。43284 所需 IPsec 参数由选定 launcher 的运行时创建和释放。

### 选定数据流

`App Flow (SequentialFlow, one exec step)` → `ExecutableBackendProcess`（先选唯一 launcher）→ (Shizuku 已授权：`ShizukuBackendClient` → 现有 `GhostlockUserService` → `ShizukuBackendHosts` → shell UserService `ProcessBuilder.exec` 原版 ELF；否则：App-private nativeLibraryDir → App `ProcessBuilder.exec` 原版 ELF)。两种 launcher 各自拥有 runDir、输入和日志，不能跨进程传用绝对路径；不确定的 Shizuku 启动结果不会回退并启动第二个 child。

- 43499：选定 launcher 把本次 profile 输入写入其独立 runDir；Shizuku Bundle 仅传 ELF 名称、固定 argv、文件清单及 `.conf` 字节，profile binary 经 payload 传递；host/App launcher 均直接 `exec` 同名原版参数约定的 ELF，并按原版无长度帧 stdin 约定写 binary。两路 runDir 分别为 shell-owned `/data/local/tmp/ghostlock-glue/<backend>/<run-id>` 与 App-private `filesDir/ghostlock-glue/<backend>/<run-id>`。上游 `main.cpp` 与 C/C++ 文件保持不变。
- 43284：App Flow 只选后端并提交空 payload；host 使用 UserService `Context` 创建 DFRoot `IpSecManager` runtime，按原版 `exp.c` 的 CLI 解析器生成 `--encap-port/--sender-port/--spi/--aes-key` argv，在 ELF 退出、准备失败或 host destroy 时关闭 transform、SPI 与 encapsulation socket。密钥只在内存参数中存活，不写入日志/配置/文档。
- host stdout/stderr 重定向到 run log 文件；共用 UserService 日志泵按偏移回传，避免 pipe 读取在 native 时序窗口造成 backpressure。app 收到的行同时写入 app 私有 run log。terminal evidence 由 exit status/verify 产生。
- `destroy()` 不增加 kill/signal AIDL；Flow 只在 terminal 后 `disconnect(remove=true)`。失败、binder disconnect、取消和 destroy 的资源终结顺序纳入实现与 fake-host 测试。

### 改动范围与保留

- 修改：`ExecutableBackendProcess.kt`、`FlowLaunchParent.kt`、`GhostlockUserService.kt`、`ShizukuBackendHost.kt`、`ShizukuBackendClient.kt`；在 `43499glued/`、`43284glued/` 增加后端 host adapter/runtime；增加共享 Shizuku host stable IDs；新增 Flow/host contract JVM tests；同步本计划与 `full-process-uml.md` exec 流程图。
- 保留：`SequentialFlow` 各一个 exec step；backend 选择 allowlist；43499 card/上游 profile 与转换器；通用 Finish/Payload 文件业务；上游 C/C++ 原文与 ELF entrypoint；原版后端和 Lua disabled；按本次 availability snapshot 只选一个 App 或 Shizuku launcher；无远端失败后的第二 child、无第二个 UserService/AIDL、无新 native route。
- 回滚：用本批 `DIFF_FILE.patch` + `ROLLBACK.sh` 在复制树逆转；不对实际工作树运行回滚。构建/安装产物保留于实际 `build/`。

### 验证矩阵

1. fake Shizuku port 测试：绑定失败/无授权、prepare 非零、start 非正、日志分块、terminal exit 0/非 0、disconnect；断言无 app ProcessBuilder 回退。
2. 43499 输入：无 profile 在 Gate 拒绝；有效内置/导入 profile 到 host 的 stdin 字节逐字节等于 `Profile.toBinary()`；conf/bin 落盘；CLI 单一 `--ghostlock-app-call`。
3. 43284 输入：host argv 与原版 parser 所需选项闭包一致；IPsec runtime 参数只来自当前 run；runtime 在 terminal/failure/destroy 均 close。
4. 主机：`./gradlew :app:assembleDebug`、针对性单测、`git diff --check`、profile 与 upstream manifest/hash 对拍；APK zip/DEX 及两 ELF arch/entrypoint 核验。
5. A301SO：先验证 Shizuku 服务连接/uid/context/权限，再安装并核 APK hash、versionName/versionCode、selector/profile card 与普通 Shizuku ping。此批部署验证不执行漏洞 ELF；只有用户另行要求时才进入攻击路径真机门禁与归档。

### 批次边界

本设计触及 exec/AIDL/进程资源生命周期，属于 L 级；design-critic 阻断项关闭后再实现。首批只打通两个 Glued exec 后端与共用 Shizuku host，不改上游源码、JNI 后端、攻击原语或 kernel profile schema。

## 历史设计 rev2（2026-10-29；已由 rev3 取代，不可按本节执行）

> 历史状态：本节曾提出 Shizuku JVM 执行同一 Flow 且禁止 App fallback；这两项执行位置决策已被 rev3 取代。当前唯一有效的执行位置与 fallback 决策见 rev3。

### 活跃计划关系

- `98260-user-requirements-summary.md` §0/A12 禁止迁移的是**原版** 43499/43284，并未禁止新增不同身份的 Glued backend；原版保留且禁用，Glued 用新 ID。`98260-backend-plan-delta-userservice.md` §2.1 的共用 `FlowSymbolInvocation`/Shizuku UserService 执行同一 Flow 仍为执行架构权威。因此无冲突，本计划只扩展两个新 backend。
- rev1 的「App Flow + Shizuku 只运行 ELF」与共用 symbol/Flow 方案冲突，**由 rev2 取代**。本计划是本工作树唯一 Glued 计划，不再创建并行计划。

### 执行图与单一权威

`UI → BackendRunDispatcher → FlowLaunchParent(symbol + bounded invocation)`。App 模式在 App JVM 用 `LocalThreadFlowExecutor` 执行该 runner；Shizuku 模式由唯一 `GhostlockUserService` 按同一 stable symbol 在 shell JVM 解出同一个 `BackendSpec.runner`。两个 Flow 都是 `SequentialFlow` 且各只有一个 `exec` step；该 step 调用共享 `ExecutableBackendProcess` 直接启动固定 ELF 路径及 argv 列表，禁止 shell 字符串拼接和 app UID 静默回退。

stable backend IDs：`43499_GLUED`、`43284_GLUED`；对应 Flow symbols 必须严格为 `43499-Glued`、`43284Glued`。Service 端 identity/symbol 双重校验；原版 CVE、Lua、任意未知 symbol 拒绝。两个 backend declaration 分属 peer 文件，不建 `BackendExecGlue` 继承类；`ExecutableBackendProcess` 是共享 launcher，不是 backend spec。

### Wire、临时文件与进程状态

- 复用共用 AIDL 生命周期 `prepare(backendId, Bundle config, byte[] payload) → start → getState/readLog/verify → terminal/destroy`。固定 schema version、`flow_symbol`、canonical UUID `run_id` 和 codec key allowlist；ELF 名由 backend ID 映射为常量，禁止调用方传 executable path、cwd、任意 argv、shell fragment 或 app-private path。
- `Bundle + byte[]` 总序列化预算 ≤512 KiB（Binder 1 MiB 上限留至少 512 KiB）；超限由 App 在 Binder 前拒绝、host 再复核。字段缺失、未知/重复 key、backend-symbol 不匹配、负/越界值、UUID 路径穿越、绝对路径均 fail-closed。测量所有内置 profile、Finish/Payload 最大尺寸；任一真实输入超过预算则改用 PFD 分片协议，不提高事务预算。
- Shizuku host 写入 shell-owned `/data/local/tmp/ghostlock-glue/<backend>/<UUID>/` 下固定文件名；App launcher 使用 App-private `filesDir/ghostlock-glue/<backend>/<UUID>/`。`GHOSTLOCK_HOME` 指向本次 run dir。launcher 只注入三个路径键：`GHOSTLOCK_HOME`、`TMPDIR`、`GHOSTLOCK_KSU_LOG`；其中前两者指向该 launcher 的 runDir，KSU log 与 `run.log` 分开。`GHOSTLOCK_KSUD` 不由 launcher 注入，保留 upstream search/fallback。App-side SAF URI 不传 host；所选 conf 与脚本内容由 App 读取后放入受限 invocation。
- owner：host Flow/launcher 唯一拥有 executable child、run dir、log source、43284 `IpSecRuntime`；状态 `IDLE → PREPARED → RUNNING → TERMINAL → DESTROYED`。Prepare/start/exec failure、cancel、binder death、service destroy、重复 destroy 都要先停止并 wait child、再 close runtime/fd、最后清 run files。UserService `System.exit(0)` 只能发生在 host terminal 且 child 已 reaped 后；无 active child orphan。需核对上游 exec 与进程树并在实现测试固定 child-descendant 语义。

### 有界日志

- child stdout/stderr 有专用 drain owner，UI/AIDL polling 不直接读 child pipe。单 run 最多保留 8 MiB，AIDL 单次 `readLog` ≤32 KiB；offset 非负、单一 reader 顺序推进，terminal 后 drain 至 EOF 后才 disconnect。上限溢出采用有界 ring/truncation marker 和 dropped-byte count，不允许无限增长。
- bytes 跨 AIDL 回传，App 侧以状态化 UTF-8 decoder 拼接跨 chunk 字符；读失败/offset 越界/host log persistence failure 有具名结果。日志落盘故障不阻塞 child drain，也不回滚已发生的执行；不会把 partial UTF-8 替换成乱码。

### 43499 配置、Finish/Payload 与上游脚本

- 43499 card 从 `origin/main` 的 `kernel_profiles/index.conf` 列出 built-in `.conf`，支持外部自包含 HOCON 导入；只用上游 `LegacyProfileConverter.kt`。App 在 Flow invocation 中携带原 conf bytes、经 `ProfileResolver` 校验后的 GLK1 bytes 和通用 finish/payload 脚本 bytes；无效/未选 profile 由 readiness/Gate 拒绝，不 fallback 到旧本地配置。
- **设计阻断项**：`app/core/finish/FinishScript.kt` 必须逐项覆盖 `app/src/main/cpp/43499glued/attack/ops.cpp:226-431` 原脚本行为（SELinux policy 修复/KMI 与 ksud late-load/safe mode/debug dump/失败路径等），否则不得让 common script 取代上游最终脚本。等价后，host 将 common script 以固定 `.ghostlock_root.sh` 写到 run dir、禁止写入，再设置 `GHOSTLOCK_HOME`；只有源码与测试证明原版 ELF 按既有路径尝试 `open(O_TRUNC)` 失败后仍继续至 victim exec，才接受该桥接。上游 C/C++ 文件保持逐字节未改；finish/payload 业务逻辑只在 `app/core/`，UI 仍在现有 UI 目录。

### 43284 上游参数与资源状态机

- 按 `src/upstream/43284/exp.c:507-538` 映射：`--encap-port`/`--sender-port` 为十进制 port；`--spi` 为 base-0 解析且落入 uint32；`--aes-key` 为 32-byte AES key 的 64 个 hex 字符。任何缺参、超界、长度/格式错都拒绝；单测逐 argv token 对拍 C parser。key 仅在 shell JVM 内存存在，不写 conf、Bundle、日志或 run files。
- `IpSecRuntime` 只属于 43284 Flow：创建 UDP encapsulation socket → 分配 loopback SPI → 临时 `DatagramSocket` 取 sender port 后立即关闭 → build transform → exec ELF → terminal 后按 reverse acquisition close transform、release SPI、close encapsulation socket。对照 DFRoot `ExploitRunner.java` 上游实现，不调用上游未调用的 apply；失败/取消/重复 close 均走 idempotent state machine，每项资源恰好释放一次。上游 Java 来源固定为 DFRoot commit `49e992ef3c9f7ec03c8efea6e3cf552f7ec6a347`，C parser 以本地 manifest/file 为准。

### CBN（成本权衡）

| 决策点 | 备选 | 可量化收益 | 工程成本与边际扩展成本 | 翻转条件 | 结论 |
|---|---|---|---|---|---|
| Shizuku 执行位置 | A. UserService 按 symbol 执行同一 Flow；B. App Flow 只远程启动 ELF | A 维持 App/Shizuku 共用 1 个 Flow symbol/runner；两 backend 各 1 个 exec step。B 少 1 个 service Flow-dispatch seam | A 增 1 个共用 dispatcher/codec + 2 个 backend input codec；新增 backend 增 1 个 stable ID/codec/test。B 每 backend 要保持 app Flow 与 remote exec 两套参数/状态契约 | UserService 无法取得且 App 无法序列化必需输入时，暂停该 backend 并设计 PFD，不切成双权威 Flow | A；符合共用 Flow/UserService 架构 |
| 配置文件传输 | A. bounded Bundle+byte[]；B. PFD/分片 | A 当前预计每 run 1 次 Binder prepare、0 个额外 fd owner；B 支持大文件 | A codec 每 backend 声明字段；B 每文件至少新增 open/write/close 三段生命周期与 offset/retry | 实测 profile+finish+payload 任一超过 512 KiB 时强制选 B | 先选 A，尺寸测量通过后实现 |
| 子进程日志 | A. 有界 8 MiB ring + 32 KiB chunk；B. 直接写无上限文件 | A 每 run 硬上限 8 MiB、单 AIDL txn ≤32 KiB；B 无有限资源上界 | A 需一个 ring/offset owner，新增消费者复用；B 每 run 有磁盘增长风险且缺全局清理上界 | 任何验收必需日志超过保留量则改分段保留，总量仍须有界 | A；drain 与 UI 解耦 |

### 改动边界与留证

- 修改候选：`FlowLaunchParent.kt`、`BackendFlowRunner.kt`/Flow invocation codec、共用 AIDL/service/client/host registry、两个 peer Glued adapter/runtime、`ExecutableBackendProcess.kt`、`app/core/finish` 与 `app/core/payload`、相关 JVM/Android tests，以及 `docs/development/full-process-uml.md` 的同批 execution sequence/state diagram。
- 保留：原版 CVE 与 Lua 在 UI 显示但禁用；CMake 原版 JNI targets 继续注释；`cve43499glued`/`cve43284glued` 为 peer，不建统一 glued/core 目录；SequentialFlow 单 exec；profile index/converter 与上游原文不改；无 per-backend UserService/AIDL、无第二个 Flow authority。
- 新实现批使用独立事务 artifact；不覆盖当前 `docs/exec-glue/DIFF_FILE.patch`、`VERIFICATION.txt`、`ROLLBACK.sh`（它们记录前一批 UI/profile correction）。回滚只在独立副本测试，不对主工作树执行。

### 验证矩阵（D1 黑箱、白箱、极值）

1. 黑箱符号分发：APP 与 SHIZUKU 两路径解析同一两个 runner；未知 symbol、原版 ID、Lua ID、backend/symbol 不匹配均拒绝；每个 Flow 恰为一个 `exec` step。
2. AIDL 极值：空配置、512 KiB、512 KiB+1；缺键/未知键/无效 UUID/路径穿越/任意 argv；两端拒绝一致且超限调用不进入 Binder。
3. 白箱资源生命周期：prepare/start/exec/nonzero exit/cancel/binder death/repeated destroy；child wait/reap 一次，fd/runtime/run dir close 一次；无 orphan；terminal 前禁止 remove service。
4. 日志：UTF-8 字符跨 chunk、offset 重复/越界/并发、8 MiB+1 截断、terminal tail drain、App 写盘失败；证明 child drain 不受 consumer 背压影响。
5. 43499：85 个 upstream profile + converter 哈希；built-in/import parity；conf 与 GLK1 bytes 对拍；common finish 与 upstream 每项行为覆盖；`O_TRUNC` 失败后继续 exec 的源码/host 测试；source manifest 无变化。
6. 43284：parser 每 token 有效/无效向量；key 在日志/磁盘扫描中缺席；resource failure state machine 每阶段与 repeated close 测试。
7. 主机 APK：`assembleDebug`、针对性测试、`git diff --check`、manifest hash、两 ELF 的 `EM_AARCH64/ET_EXEC` 与 entrypoint、APK zip/DEX/resource/version 核验。
8. A301SO：安装后核 version/hash；确认 Shizuku 授权/service UID/连接；经 test-only fixture 在同一 service/Flow/exec 通路启动无副作用 stub 并核 exit/log；43284 只验证 IpSec runtime create/close。本批不运行任何漏洞 ELF，也不宣称攻击路径 supported。

### rev2 历史批次门（已由 rev3 取代）

L 级设计须 design-critic 复核、阻断清零并获用户认可后才改生产源；之后单 writer 串行构建。当前 rev2 状态：待 design-critic 复核与用户批准；部署链未完成。


### 复核落实与来源对拍（2026-10-28）

#### 新 Glued 身份与原版 backend 边界

design-critic 复核原始用户要求后确认：“新增临时后端”且为“新 app/shizuku 进程启动器 + flow 执行入口点”足以说明两个 Glued 是独立新 backend 身份，可在 Shizuku JVM 执行；这不迁移原版 43499/43284。边界固定如下：

| 身份 | 执行身份与保留 | 允许复用 | 禁止复用/改变 |
|---|---|---|---|
| 原版 43499 / 43284 | 原版 backend 保持既有 App 域语义；本工作树 UI 禁用；原版 CMake target 暂停 | 不增加新依赖 | 不迁移到 Shizuku；不改变 W3、JNI、session/route 与生命周期 |
| 43499-Glued / 43284Glued | 独立 stable ID；各自一个 SequentialFlow exec step；可由 App 或共用 Shizuku UserService 执行 | 只复用 FlowSymbolInvocation / stable symbol 入口、共用服务与日志通道 | 不借用原版 JNI、session、route 或攻击生命周期；不增加 BackendExecGlue 继承层 |

#### 上游 profile/converter 字节对拍

以 origin/main 的 437b3b257462e17d395cfd2cd8fb81536d6c6358 为基线，对 kernel_profiles 目录 85 个资产（84 个 .conf profile + index.conf）及 LegacyProfileConverter.kt 共 86 个文件逐字节比较：86 检查、0 缺失、0 不同。index.conf SHA-256：1f0efc66a8613aaec037ea427af261e9e685d0f69172327679076f6a595f8791；LegacyProfileConverter.kt SHA-256：947c2024ff8a6e0f4986301bed18460dcc0b62e5eb41425f9d071cbfb903465c。路径卡片由 Glued43499ProfileCodec 从 kernel_profiles/index.conf 加载 built-in，并通过 ACTION_OPEN_DOCUMENT 读取导入 URI；不读取旧 assets/profile/43499。

#### Finish 脚本等价性仍为阻断

只读对照 app/src/main/cpp/43499glued/attack/ops.cpp:226-431 与 app/src/main/kotlin/com/ghostlock/app/core/finish/FinishScript.kt:17-65 后，common FinishScript 不是上游 write_root_script 的等价实现：上游包含 root uid gate、ksud 多路径查找与 fallback、iomem 原子缓存、debug dump、checkreqprot、KMI 解析、安全模式停用模块、SELinux policy 修复/重试、KernelSU late-load 与模块状态等待；当前 common script 不包含上述链路。上游以 O_TRUNC 写 root script；运行目录中预写/禁止写的桥接行为及失败后是否继续 exec 也尚未由测试证明。故不能以当前 common script 替换上游最终脚本；该阻断保留至逐项覆盖与桥接测试通过。

#### 当前部署阻断的代码物证

- app/src/main/kotlin/com/ghostlock/app/ui/backend/FlowLaunchParent.kt:112 仍返回 flow-launcher:shizuku-remote-pending。
- app/src/main/kotlin/com/ghostlock/app/shizuku/GhostlockUserService.kt:63 调用 host registry；当前 registry 仅注册 98260，两个 Glued stable ID 尚无 host adapter。
- app/src/main/kotlin/com/ghostlock/app/backend/core/ExecutableBackendProcess.kt:51 仍在 App 进程中 ProcessBuilder.start()。
- 43284Glued 的 Flow 仍提交空 argv；尚无按上游 parser 生成的参数与 IpSecRuntime 生命周期。

【rev2 历史复核记录】本次复核关闭 Shizuku 新身份歧义，并补齐 profile 来源证据；Finish 行为等价、Shizuku host/Flow 接线、43284 参数与资源生命周期仍是部署前阻断。该结论随后由 rev3 用户路由裁定更新；不得按 rev2 执行。


### 设计评审状态更正（2026-10-28）

【rev2 历史状态】上一节“待 design-critic 复核与用户批准”是复核前状态。design-critic 对 HEAD b119085915748a940740d7ce919b5a30ed8e9346、计划快照 SHA-256 d6291ade5ff88480e0c7acad7b6cf8230f551cc7627bd3f62025268c3c2f0e34 的最终只读复核已完成：A12/new-Glued 身份冲突关闭，边界表充分，profile parity 关闭。随后用户明确修订为 rev3 双 launcher 路由；rev2 不再是活跃审批对象。


## 唯一活跃设计：最终部署批次设计 rev3（2026-10-28；按用户最新执行器裁定；取代 rev2 冲突项）

> rev2 中“Shizuku JVM 执行同一 Flow”与“禁止 App fallback”仅为历史方案；当前仅按本节 rev3 设计实施。

### 用户裁定与单一 Flow 权威

Shizuku 不是运行前置条件。两个 Glued backend 都保留 App 进程启动器与 Shizuku 进程启动器；每次 run 由 app 在 Flow 入口检查 Shizuku 授权：已授权时走 Shizuku launcher，未授权/不可用时走 App launcher。用户原话：“shizuku并非必须，和app的进程启动器并行，如果有shizuku授权则通过shizuku启动进程，否则通过app权限”。

**执行器选择补充**：两个 Glued backend 的 Flow 始终由 App 持有；exec 阶段以 Shizuku binder 可达且权限已授予作为路由条件，满足则走 Shizuku process launcher，否则走 App process launcher。该选择不读取手动 `ExecutorPreference`；授权后 Shizuku 绑定、prepare 或 start 失败时保留 Shizuku 失败结果，不再启动 App 子进程；此规则不改变原版 backend 的执行语义。

Flow 与编排权威只在 App：UI → BackendRunDispatcher → FlowLaunchParent → 每 backend 的 SequentialFlow（各一个 exec step）→ ExecProcessLauncher。exec step 把同一份固定 ELF identity、argv/config、stdin/profile、finish/payload、run/log 交给选中的 process launcher。Shizuku UserService 只负责在 shell JVM 以固定 allowlist 启动/观测/回收指定 ELF；不解析 Flow symbol、不复制 SequentialFlow、不成为第二个编排权威。App launcher 在 App UID 下 ProcessBuilder 启动相同 ELF。原版 backend 仍禁用且不迁移。

| 路由条件 | 进程执行位置 | 失败语义 |
|---|---|---|
| Shizuku 已授权且 service 可用 | 共用 Shizuku UserService / shell JVM | 该 run 留在 Shizuku 路由；prepare/start/connection 不确定或失败时返回具名失败，不再自动启动 App 子进程，避免远端已启动但响应丢失时重复执行 |
| Shizuku 未授权或未运行 | App JVM / App UID | 用 App 权限直接 exec；本地启动/文件/API 权限不足时返回具名 NotReady/Failed，不伪造成功，也不尝试原版 JNI |

分支在任何 exec child 启动前确定；同一 run 恰好一个 launcher owner。Shizuku 与 App 仅共享 runner 输入契约和 terminal 结果形状，不共享子进程、fd、临时目录、IpSecRuntime 或执行状态对象。

### 组件与生命周期调整

- App 内新增窄的 ExecProcessLauncher 接口；AppExecutableLauncher 复用本地固定 ELF 执行能力；ShizukuExecutableLauncher 通过共用 AIDL/UserService 使用 backend ID 映射固定 ELF。禁止调用方提供任意路径、cwd、argv shell 字符串或 Shizuku URI。
- AIDL 传输继续采用固定 schema/字段 allowlist 和 bounded payload；Service 每个 Glued ID 使用 peer host adapter。Service 管理的进程仅由 host adapter 创建/等待；App-local 分支由 App launcher 创建/等待；两者各自全有或全无持有进程句柄与 log offset。
- 取消 rev2 的 host-side FlowSymbolInvocation resolver/第二个 Flow runner；Flow symbol 只在 App 本地 resolver 解析。保留 stable backend ID → host adapter 映射以校验远端 ELF 身份。
- 43499 profile/config 与 finish/payload 文件先由 App Flow 校验生成；两条 launcher 路径将相同字节交给对应执行进程。FinishScript 与上游 ops.cpp 行为不等价仍是 43499 deployment blocker，未完成 parity 与 O_TRUNC 桥接证据前不将 common script 替代上游最终行为。
- 43284 运行参数仍按 DFRoot parser 构造；IpSecRuntime 由实际选中 launcher 所在进程的 runtime adapter 创建/关闭。App 分支只有在其 app-domain API/权限可满足时才 ready；否则具名拒绝该次运行，不把“可启动 ELF”误报为“43284 可运行”。Shizuku 分支使用 UserService Context。两分支均 reverse-release 且不共享 runtime。

### rev3 专属验证门

1. Shizuku 授权/运行时 route matrix：授权并 service 可用 → 只调用 Shizuku launcher；未授权/未运行 → 只调用 App launcher；远端 prepare/start/断连模糊态 → 不启动第二个 child。fake port 断言每 run 单一 spawn owner。
2. 同一 App Flow/symbol/config 在两条 launcher 中保持固定 ELF identity/argv/profile/script bytes 对拍；original CVE/Lua ID、unknown ID、path/argv injection fail-closed；每个 Glued Flow 恰为一个 exec step。
3. App launcher 与 Shizuku host 分别测 process start/nonzero exit/log terminal/child wait/reap/failure cleanup；log 硬上限与 offset/decode 行为一致；不跨进程复用 fd/runtime。
4. 43284 两路分别验证 parser 参数；只有具备所需 IpSec 权限时 App route 才 ready，权限缺失具名拒绝；Shizuku route 验证 IpSecRuntime acquire/close reverse order。
5. A301SO smoke 在真实 Shizuku授权状态下分别验证 Shizuku 与 App 路由的无副作用 test fixture、版本/APK hash、日志/terminal；此批仍不运行漏洞 ELF。

rev3 保留 rev2 的结构约束、profile 来源和 C/C++ 上游原文；仅替换“Shizuku JVM 执行同一 Flow”与“禁止 App fallback”的执行路由决策。用户对审批问题明确裁定为“Shizuku 非必须；有授权走 Shizuku launcher，否则走 app launcher”后，按该双 launcher 决策进入实现；旧的 5 个 deployment blockers 均继续跟踪，并增加 App-domain 43284 IpSec capability gate。

### 实施进度（2026-10-28；批次 1：双进程启动路由）

- 已实现：Glued Flow 固定由 App 持有；运行门禁忽略手动 Shizuku 选择对这两个 backend 的阻断；每次 exec 在 spawn 前按 binder+permission 选择 Shizuku 或 App；选中 Shizuku 后出现 prepare/start/连接失败不转启第二个 App child。
- 已接线：公用 UserService 固定注册两个 Glued ID；host 只允许映射后的 ELF、文件名 allowlist、结构化 argv 与 ≤512 KiB 输入；本地/远端日志保留上限为 8 MiB，远端 host 等待并在 destroy 时终结其直接 child。上游 fork/setsid 后代的取消/断连语义尚未完成验证，仍是部署门禁。
- 验证：./gradlew :app:testDebugUnitTest --no-daemon --console=plain 成功（406 tests，0 failures/errors/skipped）；./gradlew :app:assembleDebug --no-daemon --console=plain 成功；2.0.12 (1342) APK 已安装到 AVD 并打开 Paths 页，selector 中仅两项 Glued 可选，原版 43499/43284/98260 与 Custom 显示为灰色。
- 未完成：A301SO 尚未安装本批 APK；无副作用进程 fixture 的 Shizuku/App 双路真机 smoke 尚未完成；不执行漏洞 ELF；43499 FinishScript/O_TRUNC parity、43284 parser 参数与 IpSecRuntime 生命周期仍是部署阻断。

### rev3 批次 2：43284 launcher-domain IpSec runtime（2026-10-29）

- 来源核对：DFRoot commit `49e992ef3c9f7ec03c8efea6e3cf552f7ec6a347` 的 `app/src/main/java/df/root/ExploitRunner.java` 建立 UDP encapsulation socket、loopback SPI、32-byte AES key 与临时 sender port，再建立 AES-CBC transform；C parser 要求 `--encap-port/--sender-port/--spi/--aes-key`。本工作树的 Glued C 原文不变。
- 已实现：App 路由在 App Context 创建独立 `AndroidIpSecRuntime`，由 argv formatter 生成上游四组选项；Shizuku 路由在 43284 host 的 UserService Context 创建另一份 runtime。参数不由 Flow/Binder 输入；43284 来自调用者的 argv 必须为空，profile/key 不写文件或日志。AES 字节在 close 时清零，父进程 argv 容器在 spawn 后清空。
- 生命周期：sender `DatagramSocket` 取端口后立即关闭；transform → SPI → encapsulation socket 按逆序在 foreground ELF terminal/cancel/destroy 后释放。Shizuku prepare/setup 失败会在 spawn 前拒绝；host close 失败返回具名结果。
- 主机验证：改前 `:app:testDebugUnitTest` 为 406 tests / 0 failures/errors/skipped；本批最新完整结果 410 tests / 0 failures/errors/skipped。`assembleDebug` 与 `assembleDebugAndroidTest` 编译通过；单测先暴露过 CloseScope 反向列表 view 被清空的缺陷，已改为 snapshot 并复测通过。
- 设备门：本批仍需 A301SO instrumentation 只验证 App-domain runtime create/close；不调用 `start()`、不运行 ELF。Shizuku 当前未运行，故 shell/UserService runtime smoke 尚未执行。另有 43499 FinishScript/O_TRUNC parity、43499/43284 fork/setsid 取消语义和无副作用 exec fixture 仍阻止最终可用性结论。

#### 批次 2 设备门结果（2026-10-29）

- A301SO：debug APK `com.ghostlock.app` 已更新为 2.0.12 / versionCode 1342，installed base.apk SHA-256 `a4b49a419ef3513661d3b2f9a98c71cf9c8dbe8a73698be578c1ce803991b7c2`，与本地产物逐字节一致；`MainActivity` 重新启动且为 ResumedActivity。设备当时处于 Dozing/keyguard 状态，因此没有将其记为物理屏幕截图/UI 可视门通过。
- 命令 `adb -s adb-QV770MFGJ1-XxNlpb._adb-tls-connect._tcp shell am instrument -w -e class com.ghostlock.app.backend.cve43284glued.AndroidIpSecRuntimeSmokeTest com.ghostlock.app.test/androidx.test.runner.AndroidJUnitRunner` 返回 `OK (1 test)` / exit 0：只在 App UID 建立、校验并逆序关闭 IpSec runtime；测试不调用 `ExecutableBackendProcess.start()`、不运行任何漏洞 ELF。测试 APK随后卸载。
- A301SO 上 Shizuku manager 包已安装，但没有运行中的 Shizuku/UserService 进程；本次设备验证覆盖当前可用的 App-domain route，Shizuku-host runtime 仍未真机验证。原版 UI 的 AVD selector/profile card 证据保存在 `transaction-a301so-deploy/avd-main.png`。
- 当前实现消除了 43284 参数/IpSec runtime 这一阻断，但完整部署仍被 43499 FinishScript/O_TRUNC parity、两上游 fork/setsid descendants 的取消/断连归属，以及两条 exec launcher 的无副作用进程 fixture smoke 阻断；不运行漏洞 ELF。

### rev3 批次 3：恢复 DFRoot 上游 C 原文（2026-10-29）

- 复核发现 `app/src/main/cpp/43284glued/exp.c` 曾为接入通用日志而引入 `df_log.h` 并替换上游 `printf` 调用，不符合“DFRoot C 代码不做修改”的原始要求。
- 已按固定上游快照 `/tmp/DFRoot-upstream-inspect`（commit `49e992ef3c9f7ec03c8efea6e3cf552f7ec6a347`）恢复 `exp.c` 原始字节；移除仅供 Glued 43284 使用的 `df_log.c/.h`，其输出由既有 exec stdout 捕获进入通用日志；CMake 只保留可执行文件打包所需适配。
- 验证结果：`exp.c` 与 pinned DFRoot `app/src/main/jni/exp.c` 逐字节一致（`True`，SHA-256 `afdd68cb6b07fa9b04f93c7d4bd9f8ce0333f4dd2265416959cf9c630d55ac39`，22,927 bytes）；`./gradlew :app:testDebugUnitTest --no-daemon --console=plain` 成功，build/test XML 为 410 tests / 0 failures / 0 errors / 0 skipped；`./gradlew :app:assembleDebug :app:assembleDebugAndroidTest --no-daemon --console=plain` 成功（77 actionable tasks）。
- AVD/A301SO：当前 debug APK SHA-256 `82f117e974217587d769009cce0f54eb542e4536625e320a9d963394035559b7` 已分别安装到 AVD 与 A301SO；两端 `base.apk` 均与构建产物逐字节一致。A301SO（Android 15）`AndroidIpSecRuntimeSmokeTest` 返回 `OK (1 test)`，随后测试 APK 卸载；没有漏洞 ELF 进程。AVD Paths 页确认两个 Glued 选项可选，原版/Custom 灰显；43284 卡无 profile 配置，43499 卡提供内置 profile 与 `Import .conf`。
- 尚存 UI 文案脱轨：AVD Home XML 仍显示 `GhostLock is running with demo data; chain execution is not wired yet.`，与当前已接线的 Glued exec 路径不一致；本批只记录，未改 UI 文案。
- 该修改只恢复上游原文与 CMake source list，不运行漏洞 ELF；批次 3 的 IpSec smoke 直接验证 App-domain runtime。随后独立的 Shizuku probe 绑定 UserService 并验证错误输入在 spawn 前被拒绝，仍未启动 Glued ELF。

#### Shizuku route probe 复核（2026-10-29）

- 更新：A301SO 此时 `shizuku_server` 正在运行；instrumentation `ShizukuRouteProbeTest` 共 2 tests，实测 `binder=true permission=true route=SHIZUKU preflight=[]`；通过 Shizuku UserService/AIDL 发送空 Bundle 后收到 `exec:host:argv-missing`，测试随后 `disconnect(remove=true)`。此前批次 2 的“Shizuku 未运行”只描述其当时观测状态，不能用于当前路由判断。
- 已验证 Shizuku binder/授权/UID/SELinux preflight、UserService bind、AIDL dispatch 及 host fail-closed prepare；该探针没有启动 Glued ELF。A301SO 的下一次 Glued run 会选择 Shizuku；成功 prepare/start、子进程终态/回收及无副作用子进程执行链仍未端到端实测。Shizuku 选中后出错不得 fallback 到 App 子进程。

#### Home 状态提示文案校准（2026-10-29）

- AVD 实测 Home 状态卡片仍显示“chain execution is not wired yet”，与当前 Glued Flow/exec 接线不符。英/中提示现明确限定为 Home 状态卡片使用演示数据，并引导到 Paths 配置后端；不宣称真实执行链已完成设备验证。
- 验证：`./gradlew :app:testDebugUnitTest --no-daemon --console=plain` 成功（410 tests，0 failures/errors/skipped）；`./gradlew :app:assembleDebug :app:assembleDebugAndroidTest --no-daemon --console=plain` 成功（77 actionable tasks）。新 APK SHA-256 `768bd2363133f940f4ac3ae6a5f354ed898f4568cd46f3beab71816f7a6e5d89` 已安装到 AVD 与 A301SO，设备 `base.apk` 与本地产物逐字节一致；AVD Home XML 显示新英文提示，A301SO `MainActivity` 为 Resumed。

#### rev3 批次 4：Glued exec fixture 与 AVD UI 验证（2026-10-29）

- 双 launcher 路由复核：App Flow 在 spawn 前检查 Shizuku binder 与授权；A301SO 当前授权/运行时 route 为 `SHIZUKU`，无 Shizuku 的 AVD route 为 `APP`。两端分别对 43499-Glued 与 43284Glued 执行只读 stdin、固定 stdout、无文件/网络/子进程副作用的 no-op fixture；未运行上游漏洞 ELF。
- 修复 Shizuku 快速 child 日志竞争：AIDL offset reader 在每轮轮询读到新日志，并在 terminal 后再 drain 一次，UTF-8 assembler 保留分块边界；以 callback pump 为输出唯一来源时，A301SO 43499 fixture 曾出现 exit 0 但 `output=[]`，测试暴露后已改正。
- instrumentation fixture 证据：AVD App route 全类运行 `OK (2 tests)`、exit 0；A301SO Shizuku route 下分别运行 43499 与 43284 单项测试时均 `OK (1 test)`、exit 0；43499 stdin 为 1,884 bytes 并回显 profile id，43284 argv_count=8 / stdin_bytes=0。一次 A301SO 全类连续运行（43284 后 43499）第二项于 5 s 后返回 `exec:shizuku:prepare:-8`；此后将同一全类按原序连续重跑 5 次，5/5 均 `OK (2 tests)`，exit 0，未复现失败。该次失败原因仍未定位，保留为观察项；未发生 App fallback。所有 fixture marker 在运行前核对。
- 直接 UI 门：AVD Paths selector 有且仅有两个 Glued 项可选，原版 43284/98260/43499 与 Custom 的选项容器 `enabled=false`；43284 卡显示 `exec (upstream CLI)` 且不显示 profile 卡；43499 卡显示上游 built-in profile selector 与 `Import .conf`。实际从 Run 页分别启动两个无副作用 fixture，终端均显示对应 backend、fixture marker、`backend run: ok`、终态及 43499 profile id。层级与截图保存在 `docs/exec-glue/transaction-safe-exec-fixture/MODIFIED_FILE/`。
- 主机验证：改前 `:app:testDebugUnitTest` 410 tests / 0 failures / 0 errors / 0 skipped；修改后复跑 413 tests / 0 failures / 0 errors / 0 skipped；`:app:assembleDebug :app:assembleDebugAndroidTest` 成功。fixture APK 只替换两个 JNI-lib APK entry 为 safe stub；活动结束后，AVD/A301SO 均安装当前 source build 2.0.12 (1342)，base.apk SHA-256=`35e63d5409d62f94769416e1c782c5ca135f69e91a24d14aa96380388b3d6ccb`，不留 fixture APK/测试 APK。
- 仍未关闭：43499 FinishScript 对 upstream `ops.cpp` 的逐项等价与 O_TRUNC 桥接；上游 fork/setsid 后代的取消/断连清理；A301SO 连续 Shizuku run 曾有一次瞬时 `prepare:-8`，其后连续重跑 5/5 通过但尚未根因定位；真实生产 ELF 未运行，故不得将攻击路径标记为 supported。当前结果证明 UI 到固定 exec 子进程、日志和终态链路的无副作用验证，不代表漏洞利用成功或完成发布门禁。

#### 批次 5：双 launcher 单所有者回归守卫（2026-10-29）

- 依据 rev3 已批准路由：Shizuku binder 可达且权限已授予时只调用 Shizuku launcher；否则只调用 App launcher。此次不改变路由条件，仅把执行选择收敛到 `ExecutableProcessRouting.dispatch`，使“远端失败不启动第二个 App child”可由纯 JVM 测试直接证伪。
- 改动：`ExecutableBackendProcess.kt`、`ExecutableProcessRoutingTest.kt`，并在本图增加 `ExecutableProcessRouting`；不改 backend ID、Flow、AIDL schema、上游 ELF/C/C++、Profile 或 UI。
- 基线：改前针对 `ExecutableProcessRoutingTest` 的 Gradle 命令成功，原两项测试通过；修改后新增选中 launcher 独占与 Shizuku 异常不 fallback 测试，路由测试 3 项通过。完整 `:app:testDebugUnitTest` 为 414 tests / 0 failures / 0 errors / 0 skipped；`assembleDebug` 与 `assembleDebugAndroidTest` 均成功。
- AVD UI：重装当前 source APK `2.0.12 (1342)` 后直接检查 Paths 页；selector 中两个 Glued 为可选样式，原版 43284/98260/43499 与 Custom 灰显；选择 43284Glued 时有 `exec (upstream CLI)`、无 43499 profile/import 控件；切回 43499-Glued 后 built-in profile 与 `Import .conf` 恢复。构建 APK 与 AVD `base.apk` 均为 SHA-256 `df6da45d3aff989e5375bc3fc172c99b45a49c7910240e4365b85777b412aad6`。截图/XML 留在 `docs/exec-glue/transaction-launcher-route/MODIFIED_FILE/`。此 UI 检查没有启动 backend/ELF。
- A301SO 部署：同一 APK 已重新安装；设备 `base.apk` 与构建产物 SHA-256 一致，`versionName=2.0.12`、`versionCode=1342`。本批未以该设备的安装记录推断 UI 可视状态或执行链成功。
- 未关闭项不变：FinishScript/upstream parity 与 `O_TRUNC` bridge、fork/setsid 后代取消/断连清理、A301SO 曾出现一次且未定位的 `prepare:-8`、真实生产 ELF 未执行；因此本批只证明 launcher 路由选择契约和 AVD UI，不宣告整体部署完成或漏洞路径 supported。

### 43499 Finish bridge 设计门复核（2026-10-29）

- 冻结快照：HEAD `b119085915748a940740d7ce919b5a30ed8e9346`；design-critic 对“预置只读 `.ghostlock_root.sh` 后复用共同 Finish/Payload”作只读复核。结论：**当前不可进入生产实现 gate**；该桥接可保留为候选，先关设计阻断，再进入用户批准与实现。
- 历史阻断（当时状态）：`payload-finish-chain-plan.md:26-32` 原要求 root 零落盘、FinishScript 只由 rooted victim 执行。后续用户原始 Glued 需求明确要求将通用 log/payload/finish 脚本直接保存到硬盘并指向上游最终脚本；该新要求仅覆盖 Glued 每 run staging，不扩展到旧 LKM/legacy 路径。当前按 Glued-only 例外推进，并保留每 run owner/mode/hash/清理及身份核验判据。
- `O_TRUNC` 狭义语义已有实证：`ops.cpp:226-234` 失败分支只返回；`cve_2026_43499_backend.cpp:453-458` 调用后继续 setup；`victim_process.cpp:142-153` 对同一路径只读打开并执行。AVD 与 A301SO 的 App UID、shell UID safe fixture 均得 `TRUNCATE_EXIT=1`、`CONTENT_UNCHANGED=yes`、`SCRIPT_EXEC_EXIT=0`、`MARKER=fixture-ok`，fixture 清理后文件不存在。证据：`docs/analysis/device-gates/20261029-0126-43499-readonly-script-bridge-fixture.md`。design-critic 关闭“写失败是否阻断静态控制流/只读执行机制”项；实际 Glued 双 launcher 的 runDir 路径、owner/mode/inode 与 native handoff 集成仍未验证。
- 其余阻断：① native root script 与共同 `RootManagerPayload` 可能重复 late-load，执行 owner 未定；② upstream `profile.safe_mode` 与 `FinishState.safeMode` 缺单一权威；③ App-private 与 Shizuku shell 的 payload staging、可读性、校验和清理未定；④ upstream `LOG`/debug dump、共同 log/marker 的映射和容量未定；⑤ detached `fork()+setsid()` worker 的完成/取消/目录清理所有权未定。
- 本复核未修改上游 C/C++ 或生产 Kotlin/native 源；不运行漏洞 ELF。只有关闭上述设计项、经 design-critic 复核和用户批准后，才实现并执行双 launcher safe fixture 集成门。

### 批次 6 设计：Shizuku 同 tag 快速重绑定竞态（2026-10-29）

- 现象：A301SO 最新源码的 no-op fixture 全类运行中，43284Glued 第一次通过；紧随其后的 43499-Glued 在 5.078 s 后返回 `exec:shizuku:prepare:-8`。Shizuku server log 显示第二个 service record 在约 0.5 s 内收到 UserService binder，但 app 未收到 `onServiceConnected`；随后 `lastReason()` 再 bind 时 server 显示 `Found existing service record`。这证明失败发生在绑定回调/生命周期，不是 ELF staging 或 child exec。
- 机制证据：Shizuku API 13.1.5 的 `Shizuku.unbindUserService(remove=true)` 不同步清空 `ShizukuServiceConnections`；其 `ShizukuServiceConnection.died()` 把 ServiceConnection 回调、connections 清理及缓存移除投递到主线程。当前客户端在 unbind 返回后立即清理本地字段，下一 run 可在 Shizuku 主线程完成该死亡回调之前复用同 tag 缓存连接。
- 行为差异：保持稳定 tag 与每 run 清除 UserService 的现有所有权；`disconnect(remove=true)` 在已建立绑定时等待 `onServiceDisconnected` 回调之后的一个主线程队列轮次，确认 Shizuku callback/cache cleanup 已退出，再允许后续 run bind。等待有界；无 binder 或未建立连接时不等待。绑定失败原因保存在 client 本地，不通过 `lastReason()` 触发第二次隐式 bind。
- 影响文件：`app/src/main/kotlin/com/ghostlock/app/shizuku/ShizukuBackendClient.kt`；`docs/development/full-process-uml.md` 的 Glued 双 launcher sequence；本计划。
- 验证：先跑 `ExecutableProcessRoutingTest` 与全量 `:app:testDebugUnitTest`；重建 debug/APK test；A301SO 用仅含固定输出/只读 stdin safe stub 的 fixture 连续运行 43284→43499 全类至少 10 次，逐次要求 `OK (2 tests)` 且 `route=SHIZUKU`；保存失败前后 logcat；最后重装基线生产 APK、卸载 instrumentation APK，并核对 installed hash 与进程列表。不得运行上游漏洞 ELF。
- 原因判定仍属根因假设，需结合 fix 后重复门通过；若死亡回调等待超时，保留具名诊断并停止后续 Shizuku rebind，不静默开第二个 launcher。

#### 批次 6 实施与设备结果（2026-10-29）

- 修改 `ShizukuBackendClient`：`disconnect(remove=true)` 在已连接时等待 `onServiceDisconnected` 回调后再经过一轮 main-looper 队列，令 Shizuku 13.1.5 的 `ShizukuServiceConnection.died()` 完成 connections 清除与 tag-cache 移除；超时为 5 s 并记 `shizuku:unbind:callback-timeout`。bind 失败写入本地具名原因，`lastReason()` 不再因 `prepare` bind timeout 触发第二次隐式 bind。稳定 tag、每 run UserService destroy、同 run 单 launcher 行为均不变。
- 失败前复现：A301SO safe fixture 全类命令 `adb -s adb-QV770MFGJ1-XxNlpb._adb-tls-connect._tcp shell am instrument -w -e expected_route SHIZUKU -e class com.ghostlock.app.backend.core.GluedExecFixtureSmokeTest com.ghostlock.app.test/androidx.test.runner.AndroidJUnitRunner`，43284 测试通过，紧接 43499 测试 `prepare:-8` / `elapsedMs=5078`；JUnit `Tests run: 2, Failures: 1`（am shell exit 0）。同一时序的 server log 显示第二个 binder 约 0.5 s 到达，失败约 5 s 后再次 bind 才 `Found existing service record`。原始输出来自 `docs/exec-glue/transaction-shizuku-rebind-20261029/MODIFIED_FILE/repeat-01.log` 与 `BASELINE_FILE/a301so-rebind-logcat.txt`。
- 最终源码矩阵：同一 final source APK 派生 fixture 只把 `lib/arm64-v8a/lib43499-Glued.so` 和 `lib43284Glued.so` 替换为 7,184-byte fixed-output/no-file/no-network/no-child safe stubs；其余 APK 条目未变。A301SO 连续 10 次执行 43284→43499 全类，每次都为 `OK (2 tests)`、shell exit 0、route=`SHIZUKU`；每项分别 exit-0，43499 stdin 1,884 bytes、43284 argv_count=8/stdin_bytes=0。原始每次输出在 `docs/exec-glue/transaction-shizuku-rebind-20261029/MODIFIED_FILE/final-repeat-01.log`…`final-repeat-10.log`；最终 logcat 捕获 18 条 exit-0 结果（两 backend 各 9 条），未见失败。
- 主机门：修改前完整 `:app:testDebugUnitTest` 414 tests / 0 failures / 0 errors / 0 skipped；删掉同批临时旧码参考块后的最终完整单测仍 414 / 0 / 0 / 0；`assembleDebug` + `assembleDebugAndroidTest` 成功（77 actionable tasks）。`git diff --check` 与三个改动文档/源码的行尾空白检查通过。
- 部署恢复：fixture 与 test APK 随后移除；A301SO 安装 final production debug APK `2.0.12 (1342)`，`base.apk` SHA-256 `50073e81341a1c4018240af39d75e7d678a23e319833f8f0c773130dd5cf080a` 与源码构建逐字节一致。AVD 同 APK hash 一致；Home/Paths 直接显示，selector 仅两项 Glued 可选、原版三个 CVE 与 Custom 禁用，43499 profile/import 卡存在。UI 证据在 `docs/exec-glue/transaction-shizuku-rebind-20261029/MODIFIED_FILE/avd-home.png`、`avd-paths.png`、`avd-selector.png` 与对应 XML。
- 设备门记录：`docs/analysis/device-gates/20261029-0147-shizuku-rebind-fixture.md`。本门只证实 launcher rebind + no-op child，未运行上游漏洞 ELF。
- 尚未完成整体部署门：43499 common Finish 与 upstream `ops.cpp` 等价/磁盘脚本桥接仍待设计裁定与逐项实现；fork/setsid 后代完成、取消及目录清理仍未证实。此次重绑定修复及 safe fixture PASS 不关闭这些项，也不标记 exploit supported。


### rev3 用户路由答复后的工作树复核与剩余设计门（2026-10-29）

- 用户对“是否批准 rev2”补充裁定：“shizuku并非必须，和app的进程启动器并行，如果有shizuku授权则通过shizuku启动进程，否则通过app权限”。按 rev3 双 launcher 路由执行；Shizuku 不作为 Glued 前置条件，流程/编排仍由 App 持有，spawn 前只选一个 launcher，已选 Shizuku 的不确定失败不回退为第二个 App child。
- 只读复核快照：HEAD `b119085915748a940740d7ce919b5a30ed8e9346`；当前 worktree 的 `BackendRunGate.kt` SHA-256 `4f3c1f5e9e700a00c90be54d1adc1ba73a275f710f33cbe300af693e9e6597ac`、`ExecutableBackendProcess.kt` `ebb351dc94f24a58e6bbb40507e6faf16083265658ae2ee426623fd051053211`、`ExecutableShizukuHost.kt` `f06c4c68449df7918c244a80e46e7f2653ff44d4fa62b2f5861e7aff0d745977`、`ShizukuBackendHost.kt` `5c41a3177b00fad02594f63675cf98ad2afc4798e2ce31a09930e2bfb5f4b1b4`、`GhostlockUserService.kt` `e2196866a8b9aed7e1af4e3ef60ee42cee9d6574334dec15c4e7d42c10b18983`。
- 代码物证：`BackendRunDispatcher` 对 `appOwnedFlowWithAutomaticProcessLauncher` 的 Glued spec 强制 App 外层 Flow；Flow 的 `ExecutableBackendProcess.run()` 再按 binder 可达且 permission granted 选 SHIZUKU，否则选 APP；两个 peer host 均已注册到共用 registry。`FlowLaunchParent.ShizukuHostFlowExecutor` 的一般 remote-pending 仍存在，但不阻断 Glued 自动 launcher；不得称为通用 Shizuku Flow 已完成。
- 设备物证：`transaction-safe-exec-fixture` 记录 AVD App route 两个无副作用 fixture 通过；`transaction-shizuku-rebind-20261029/MODIFIED_FILE/final-repeat-01.log`…`final-repeat-10.log` 记录 A301SO Shizuku route 的 10 轮 43284→43499 fixture 全部 `OK (2 tests)`，43284 argv_count=8、43499 stdin=1,884 bytes。覆盖的是 fixed safe stub 与启动/runtime/日志链，不运行生产漏洞 ELF；不作为 exploit 成功证据。
- 43284 接入边界（新增实现阻断）：`app/src/main/cpp/43284glued/CMakeLists.txt` 将 `bootstrap.c` 独立编译为 `bootstrap` executable；`bootstrap.c:196-234` 从固定 DFRoot `PREFS_PATH` 读设置并自行调用 `KSUD_STAGE late-load`，没有读取 `GHOSTLOCK_HOME`、`.ghostlock_root.sh` 或任意外部 finish path。因此“将 43284 上游最终脚本指向 common Finish”不是当前原文能支持的行为。维持 upstream C 原文的候选适配是：原 ELF/DFRoot bootstrap 保持原样，只有 ELF 成功终态后 launcher 才额外执行 common payload/finish；此候选要求两 launcher 的 root 权限与 owner、单一 late-load owner、失败语义和同一 run lifecycle 通过 design-critic 复核及用户批准，当前未批准、未实现。
- 43499 共通 Finish 的候选约束：以 `ops.cpp:226-431` 为唯一 bootstrap 行为源，确保 root UID gate、ksud 查找与 fallback、KMI、profile.safe_mode、iomem 原子缓存、checkreqprot、SELinux policy 重试、late-load/module wait、debug dump 与退出路径逐项保真；通用 payload tail 不再重复由上游拥有的 manager late-load。profile `safe_mode` 是 43499 bootstrap 权威，Finish UI 不得形成第二个值源。上游 `O_TRUNC` 写失败后继续至 victim 同路径 read/exec 的狭义证据已在 `20261029-0126-43499-readonly-script-bridge-fixture.md`；实际 Glued staging/handoff 的 inode/hash/权限/生命周期仍未验证。
- 双 launcher staging / 生命周期候选：App 与 shell 各自创建 launcher-owned 每 run 唯一目录（App-private 与 `/data/local/tmp/ghostlock-glue/<backend>/<run-id>`）；绝不把 App-private path 传给 shell；两端写入同一组输入 bytes 并核验 script/payload hash、regular-file/no-symlink、mode、owner；设置唯一允许的 `GHOSTLOCK_HOME`、`TMPDIR`、`GHOSTLOCK_KSU_LOG`。只有 child 终态且输出 pipe EOF 才能把 detached root script 视为已退出并清理；cancel/binder death/EOF 未确认时保留 run dir 与日志，不伪称无 orphan。按该合同完成独立 safe fixture 集成与 cancellation/binder-death 测试前，不进入 production finish bridge 实现。
- 当前门结论：用户新增的 launcher 路由裁定已经落实并有 safe fixture 证据；43284 runtime/参数与 host registry 不是当前阻断。剩余设计阻断为 43499 complete script parity 与 Glued path identity/lifecycle；43284 common finish 的成功后 root handoff 尚是待复核候选。不得以本节或 safe fixture 记录宣告整体部署完成。


### rev3 当前实现复核结论（design-critic，2026-10-29）

冻结对象：HEAD `b119085915748a940740d7ce919b5a30ed8e9346` + 当前 worktree（route-audit 复核附录后）；design-critic 只读，未改生产源、未跑门禁。

- 已关闭：Glued App Flow 单一权威；spawn 前按 Shizuku binder+permission 选一个 launcher；授权/可达时走 Shizuku，否则 App；选中 Shizuku 的失败不回退二次 spawn。两个 Glued stable ID/host adapters 已注册，共用 UserService/AIDL 分发。普通 `FlowLaunchParent` 的 `remote-pending` 仍仅是其它 backend 的一般 Flow executor 限制，不表示 Glued 自动 launcher 未实现。
- 已关闭：43284 App 与 Shizuku host 均有 launcher-domain `AndroidIpSecRuntime`/argv owner；A301SO safe fixture 运行证明 Shizuku host runtime 可创建并给 fixture 传 8 个上游 argv token；该证据不运行或验证生产漏洞 ELF。
- 阻断：43284 的 `bootstrap.c` 不读 `GHOSTLOCK_HOME`、`.ghostlock_root.sh` 或外部 finish path。若保留 C 原文且要求 common Finish/Payload 对 43284 生效，必须设计并获批“ELF exit-0 后 launcher 另起 root finish”阶段；需要定义执行域/uid/SELinux、只执行一次的 late-load owner、成功/失败与 marker/log 合并、取消/断连以及 IpSecRuntime 释放点。未获批前不实现该新阶段。
- 阻断：43499 common Finish 与 `ops.cpp:226-431` 尚无逐项 parity 证明；唯一 bootstrap owner 必须是上游逻辑，通用 payload tail 不得重复 `ksud late-load`；profile.safe_mode 与 Finish UI safeMode 必须归一。
- 重要：两 launcher runDir 的 owner/mode/path、预置脚本与 payload 的 identity/hash/inode、同路径 victim handoff，以及 detached child terminal/pipe EOF/cancel/binder-death 的资源终结未落成状态机或集成门。EOF 未确认时不可清理或宣称无 orphan。

验证已确认：AVD App-route 与 A301SO Shizuku-route no-op fixture；A301SO 连续 10 次 43284→43499 双 fixture；43284 runtime create/close smoke；43499 只读脚本 `O_TRUNC` 失败后继续读取/执行的双设备狭义 fixture。验证未确认：43499完整行为等价、43284 post-exit finish 权限/结果语义、双 launcher真实path/inode lifecycle、cancel/binder death descendant清理、生产ELF finish/payload；以上均为未验证，不宣称 supported。


### rev4 批次 A 设计：Glued ELF 统一由 app CMake 构建（2026-10-29）

#### 现状证据与需求脱轨

- 用户已明确要求把两个 Glued backend 作为 `app/src/main/cpp/43499glued/`、`43284glued/` 的 peer，并适配 CMake；当前 `app/build.gradle.kts` 指向 `app/src/main/cpp/CMakeLists.txt`，但根 CMake 只设置两个 Glued source-root 变量，没有 `add_subdirectory`/executable targets。
- 实际 Gradle `buildExecGlue` 另行执行 `make -B exec-glued`；`packageExecGlue` 再把结果写入 `app/src/main/jniLibs/arm64-v8a/` 源目录。`src/Makefile` 的 DFRoot 源清单仍含当前不存在的 `43284glued/df_log.c`，而 `43284glued/CMakeLists.txt` 目前仍输出 `libdfroot.so` 和 `libbootstrap.so`，与 Flow 的 `lib43284Glued.so` allowlist 不一致；该 CMake 文件也未由根 CMake 引用。
- 当前 APK 可见 `lib43284Glued.so` 与 `lib43499-Glued.so`，没有 `libbootstrap.so`。这只证明打包了主 ELF，不证明 43284 的 upstream bootstrap 可达。
- 43284 的 KO 含固定 `/data/user_de/0/df.root/bootstrap` 路径；`bootstrap.c` 又读 `/data/user_de/0/df.root/shared_prefs/dfroot.xml` 并调用 `/data/user_de/0/df.root/ksud`。本 App `applicationId` 为 `com.ghostlock.app`。保持上游 C/KO 不变时，这些固定路径与当前 App 私有目录不一致；本批不改路径、不执行漏洞 ELF，单独作为 43284 运行接入阻断，等待方案裁定。

#### 目标、范围与行为差异

- 只更正构建/打包：由现有 AGP externalNativeBuild CMake targets 生成 `lib43499-Glued.so`、`lib43284Glued.so` 与 DFRoot 内部 helper `libbootstrap.so`；目标输出到 AGP 的 `CMAKE_LIBRARY_OUTPUT_DIRECTORY`（仓库 `build/app/intermediates/cxx/<variant>/<hash>/obj/<abi>`），由 AGP native target packaging 直接收集，不新增第二个 `jniLibs` source-set。应用打包依赖 CMake variant build；删除 Gradle 对 Makefile 的构建/拷贝路径，并移除源树里由旧任务生成的两个 `.so`。
- 在根 CMake 添加两个 peer subdirectory；43499 CMake 只列上游 TU 并 link executable；43284 CMake 保留上游 `exp.c`、`bootstrap.c`、`elf_parser.c`、`libcxx.S`，移除不存在的 `df_log.c` 依赖，目标输出名适配 Glued allowlist。编译器/NDK 继续由 AGP toolchain 管理；C/C++ payload 源和 CMake 输入中的 KO/assembly 内容保持字节不变。
- 同批更新 `full-process-uml.md` 的 Gradle → CMake → generated jniLibs → APK → `nativeLibraryDir` exec 数据流。明确排除：本批不改 Flow、launcher、payload/finish 行为、upstream C 语义、profile、原版后端开关，也不运行 ELF。

#### 文件清单与验证计划

- 修改：`app/src/main/cpp/CMakeLists.txt`、`app/src/main/cpp/43284glued/CMakeLists.txt`、新增 `app/src/main/cpp/43499glued/CMakeLists.txt`、`app/build.gradle.kts`、`build.gradle.kts`、`src/Makefile`、本计划、`docs/development/full-process-uml.md`；清理 `src/Makefile` 旧 Glued 目标和生成于 source tree 的 Glued `.so`，保留该 Makefile 的既有 host/native test targets。
- 基线（2026-10-29）：`externalNativeBuildDebug + testDebugUnitTest + assembleDebug + assembleDebugAndroidTest` 成功；XML 为 416 tests / 0 failures / 0 errors / 0 skipped。基线 APK entries 为 `lib43284Glued.so`、`lib43499-Glued.so`、`libghostlock_general.so`；Glued ELF SHA-256 分别为 `1851c9c26f1db36b62f6ef1c07a759a061da434604ac132bba341ce9d4b6016f` 与 `24deae153f538fca69c3f0bd86aab7906efa5f30fff719d334b9c65f7368b36f`。确认 43284 当前 `exp.c` SHA-256 `afdd68cb6b07fa9b04f93c7d4bd9f8ce0333f4dd2265416959cf9c630d55ac39`、`bootstrap.c` SHA-256 `5b19bfe9d4c632794d52de71a513d1bac2820e0fbd474ce19813913782111ce9`；CMake baseline 尚无 Glued targets。
- 改后：从 `:app:externalNativeBuildDebug` 构建三个 executable；确认 ET_DYN PIE/AArch64、两个 backend basename allowlist、APK 中三个 Glued/helper entries、无 `app/src/main/jniLibs` Glued binaries；`:app:testDebugUnitTest` 与 `:app:assembleDebug :app:assembleDebugAndroidTest`；AVD/A301SO 只安装并核 APK hash、UI，不运行漏洞 ELF。
- 完成判据：CMake targets 与 APK entries 一对一，AGP stripped output hash 与对应 APK entry 相同，且上游 C/C++ 源 hash 未变。raw CMake output 与 APK entry hash 不同是 AGP `stripDebugDebugSymbols` 阶段的预期差异。达到此判据仍不代表 43284 bootstrap path 已接通，也不代表任一攻击路径 supported。

#### Gate 状态

本批按 M 级构建集成处理；实现只改变 CMake/Gradle artifact graph，不改攻击逻辑。另一个 L 级门是 43284 固定 DFRoot 私有路径与“C 原文不改”约束冲突；在确定 binary/package 适配且完成独立复核前，不把 43284 标成可用。

#### rev4 批次 A 实施与验证（2026-10-29）

- 实施：根 CMake 启用 CXX 并接入 `cve43499glued`、`cve43284glued` peer targets；43499 target 明列 remote/main TU，输出 `lib43499-Glued.so`；DFRoot targets 输出 `lib43284Glued.so` 与 `libbootstrap.so`。输出位于 AGP `CMAKE_LIBRARY_OUTPUT_DIRECTORY`；删除 Gradle `buildExecGlue` / `packageExecGlue` 和 merge 依赖；`src/Makefile` 保留原有 host/native tests，仅移除旧 Glued build target；删除 source-tree 两个生成 `.so`。所有 C/C++/ASM payload source 保持原字节。
- 源身份：CMake 选中的 24 个 43499 TU 逐一与 `origin/main:src/core/<path>` byte-identical；DFRoot `exp.c`、`bootstrap.c`、`elf_parser.c`、`libcxx.S`、`include.inc`、`aes256.h`、`splicehelper.c` 逐一与 pinned commit `49e992ef3c9f7ec03c8efea6e3cf552f7ec6a347` 对应源码 byte-identical。KO files 本批未与上游仓库对拍；CMake 不改写它们。`splicehelper` stripped binary 是 SOURCE-MANIFEST 记录的工作树导入 asset，不属于 DFRoot commit tracked files。
- 调试记录：第一次 `./gradlew :app:externalNativeBuildDebug --rerun-tasks --no-daemon --console=plain` 以 exit 1 结束，原因是根 `project()` 未启用 CXX，CMake 无法判定 43499 executable linker language；启用 CXX 后第二次同命令 exit 1，原因是 NDK sysroot 没有 `splicehelper.c` 使用的 `<syscall.h>`。改用已随本工作树导入并记录于 `src/upstream/SOURCE-MANIFEST.json`、SHA-256 `977741e8d5ac4b325e20eb1a2b21def505e5d6ad25b930bf6583c3dd908da64a` 的 stripped arm64 `splicehelper` asset 作 immutable input；不改 C 源，不在 source tree 生成 helper。该二进制不是 DFRoot Git commit 中的 tracked upstream file；上游 `splicehelper.c` 保持原文。
- 修改后 native build：`./gradlew :app:externalNativeBuildDebug --rerun-tasks --no-daemon --console=plain` exit 0，输出 `BUILD SUCCESSFUL in 2s`。完整验证 `./gradlew :app:testDebugUnitTest :app:assembleDebug :app:assembleDebugAndroidTest --no-daemon --console=plain` exit 0；XML 汇总 416 tests / 0 failures / 0 errors / 0 skipped。
- ELF/package：CMake raw outputs `lib43499-Glued.so` 2,233,464 B SHA-256 `4e45ad58cdd8ebe8e377a7a880d073c04e250c76466dadb8d70191d6656daab2`、`lib43284Glued.so` 1,980,688 B SHA-256 `8c57108d9fbd924d2eaade734dfd688a0c217b9d6dd4e0e1603f8641b6482179`、`libbootstrap.so` 27,144 B SHA-256 `7e207eb332acc6aca22bff19fc28a239e084234926368aa98e8d660bd0150521`，均经 `llvm-readelf -h` 确认 ELF64 / AArch64 / ET_DYN 并有 entry point。APK 中对应 stripped entries 分别为 246,376 B SHA-256 `81f14031cf143c9c67401e1e10ee33a53c971f829696d3c943764bcf8365742a`、1,937,896 B SHA-256 `42ee0ffc5581451720588c0349adb6ed5e26457ebc03e95572c4d610bc5d8079`、12,680 B SHA-256 `cd0768a5c1c8c23faef55660033f14ba110a3bad699ecdbac89a1da7e243b66e`；三者各自与 AGP stripped intermediate 逐字节 hash 相同。APK 另含 `libghostlock_general.so`，不含 `libdfroot.so`；source jniLibs 内无 Glued `.so`。最终 APK SHA-256 `4cebfb304f2f605831dd14e8579df6efa2dc80e3a14f994c22dcbcbcc37cf973`，28,999,754 B。
- 设备：该最终 APK 已分别安装 AVD `emulator-5554` 与 A301SO `adb-QV770MFGJ1-XxNlpb._adb-tls-connect._tcp`；两端拉取的 `base.apk` SHA 均与构建 APK 完全相同。版本均为 `2.0.12` / code `1342`。AVD Paths UI 直接检查：selector 中仅 `CVE-2026-43499 · Glued` 与 `CVE-2026-43284 · Glued` 可改变选择；点击灰色原版 43284 项后当前 43499 选择未改变。选择 43284 Glued 显示 ID `CVE-2026-43284-Glued`、Channel `exec (upstream CLI)`、Payload `lib43284Glued.so` 且无 profile 卡；切回 43499 后 built-in profile selector 与 `Import .conf` 显示。A301SO MainActivity 为 Resumed；本设备批次未核可视 UI。两端均未启动任何 Glued ELF。
- 本批只完成 CMake build/package、APK 安装及 AVD UI 选择验证；43284 DFRoot 固定路径、43499 Finish/O_TRUNC parity、fork/setsid descendants 取消/断连仍是未关闭项，不据此宣称可利用、supported 或部署门禁完成。


### 当前 APK 部署与 AVD UI 复核（2026-10-29）

- 复核工作树：`/Users/nickji/.codex/worktrees/exec-glue-clean/ghostlock-app`；当前工作树为 detached HEAD `b119085915748a940740d7ce919b5a30ed8e9346`，与主工作树 `refs/heads/dev-nocpp` 指向同一 commit；复核未运行 Glued 生产 ELF。
- 当前完整主机测试：`./gradlew :app:testDebugUnitTest --no-daemon --console=plain` exit 0；XML 汇总 416 tests / 0 failures / 0 errors / 0 skipped。
- AVD `emulator-5554` 与 A301SO `adb-QV770MFGJ1-XxNlpb._adb-tls-connect._tcp` 安装均为 `2.0.12 (1342)`；两台拉取的 installed `base.apk` 均为 28,999,754 B，SHA-256 `4cebfb304f2f605831dd14e8579df6efa2dc80e3a14f994c22dcbcbcc37cf973`，彼此相同。
- AVD 安装包 UI：Paths selector 显示两项 Glued 可选；原版 43284、98260、43499 与 Custom 为灰色。保持 43499-Glued 选中时逐项点按原版 43284/98260/43499 和 Custom，选择不变；43284-Glued 卡显示 ID `CVE-2026-43284-Glued`、Channel `exec (upstream CLI)`、Payload `lib43284Glued.so` 且没有 43499 profile 卡；切回 43499-Glued 后内置 profile 与 `Import .conf` 恢复。
- AVD 证据快照位于 `build/deployment-audit/AVD-disabled-menu.png`、`AVD-43284.png`、`AVD-final-43499.png`；Accessibility XML 位于同目录 `AVD-43284-ui.xml` 与 `AVD-final-state.xml`。A301SO 本轮核对安装身份/hash，未作可视 UI 结论。
- 该复核关闭当前安装包与 AVD 选择器验证项；不关闭 43499 common Finish/O_TRUNC 实际 staging 与 bootstrap parity、43284 post-exit common Finish 接线、两路 detached descendant lifecycle 等部署阻断。


### Source-compatible Finish 接入复核（design-critic，2026-10-29 03:32 America/Toronto）

- 冻结快照 HEAD：`b119085915748a940740d7ce919b5a30ed8e9346`。本复核只读，不改生产 Kotlin/native 源，不运行 Glued ELF。文件 SHA-256：`ExecutableBackendProcess.kt` `ebb351dc94f24a58e6bbb40507e6faf16083265658ae2ee426623fd051053211`；`ExecutableShizukuHost.kt` `f06c4c68449df7918c244a80e46e7f2653ff44d4fa62b2f5861e7aff0d745977`；`runtime_config.cpp` `8a250e0f92ebeeac4d3302dd0f1d4db86ad1c08809b3b4b44501f7588a0f6c58`；`ops.cpp` `6a082fbddd9dd821a13f174e98683576e666b30ee78d41d97405d376b82ba016`；`victim_process.cpp` `356e24877642940bfcd6eb3e1782674a5053713a676c297c06dea79720bc8c28`；`43284 bootstrap.c` `5b19bfe9d4c632794d52de71a513d1bac2820e0fbd474ce19813913782111ce9`。
- 路由结论：Shizuku 授权可用时选 Shizuku，否则选 App 与单一 Flow authority 相容；当前路由单测 5 项、全量 416 项通过。
- 实现缺口：两个 launcher 的 `ProcessBuilder` 仅指定 cwd，未设置 `GHOSTLOCK_HOME` / `TMPDIR` / `GHOSTLOCK_KSU_LOG`；`finish.sh` 当前只是 allowlisted auxiliary input，没有 staging 为 native 固定 `.ghostlock_root.sh`、只读 seal、对象摘要/owner/mode 校验或 handoff 证明。
- 43499 条件路径：`runtime_config.cpp` 从环境变量确定 home/root-script/log；`ops.cpp:229` 对同路径 `O_TRUNC`，`victim_process.cpp:149-153` 后续 fork/setsid worker 再按路径只读打开并执行。既有设备 fixture 只证明截断失败后的控制流；只有补齐双 launcher 固定路径 staging、权限/摘要和完成生命周期，并逐项复现 `ops.cpp:226-431` 的 bootstrap，才能保持 C++ 原文同时接入 common Finish。
- 43284 阻断：上游 `bootstrap.c:196-234` 从 DFRoot 固定 prefs 读取并自行 late-load，不消费 `GHOSTLOCK_HOME`/Finish path。当前没有证明的 launcher root 执行能力可在 ELF exit-0 后执行 common Finish；因此“上游 C/KO 原文不变”与“43284 必须执行共同 Finish/Payload”互斥，需用户裁定。
- 生命周期阻断：App 与 Shizuku launcher 目前 wait/reap 的是直接 `Process`；43499 root shell 由上游 `fork()+setsid()` 分离，直接 child terminal 不证明 Finish terminal，也不证明取消/binder death 后代已停止。
- 独立复核总判：源码原文可保留的唯一候选是 43499 只读同路径 bridge；43284 需在“保持上游原文而不接共同 Finish”与“批准 bootstrap 接线 delta”之间裁定。当前仅关闭部署包身份/AVD UI 验证，不可标记整体部署完成。


### A301SO 43284 Finish 执行权限复核（2026-10-29）

- 只读设备探测：A301SO `adb shell id` 返回 `uid=2000(shell) ... context=u:r:shell:s0`；`ps -A -o UID,PID,NAME | grep -i shizuku` 返回 `2000 5885 shizuku_server`；`command -v su` 与 `command -v ksud` 均无输出、exit 1；`ls -ld /data/adb /data/adb/ksu /data/adb/ksu/bin` 返回 `Permission denied`、exit 1。原始命令与输出留在 `build/deployment-audit/A301SO-authority-probe-20261029.txt`。未运行 Glued ELF 或触发权限提升。
- AVD UI 复验：Paths 页面当前仍显示 43499-Glued profile selector 与 `Import .conf`；点击 Finish 卡进入仍可见的两个 Finish 开关页面，点击 Payload 卡进入仍可见的 root-manager、KernelSU 选项、Safe mode 与 Save 页面。截图/XML：`build/deployment-audit/AVD-finish-page.png`、`AVD-finish-page-ui.xml`、`AVD-payload-page.png`、`AVD-payload-page-ui.xml`。这只验证 UI 展示，不证明脚本已由 Glued Flow 交接执行。
- 源码对照：`ExecutableBackendProcess.runApp()` 在 App 进程直接创建子进程；`ExecutableShizukuHost.start()` 在 UserService 创建子进程。设备证据确认 UserService 为 shell UID；App launcher 源码没有独立 root transition。`Flow43284Glued` 仅将 ELF 终态映射为 `StepResult.Done`，当前没有 post-exit Finish/Payload 阶段。
- design-critic 冻结复核结论：仅凭 App UID 或 Shizuku shell UID 在 ELF 退出后运行 common Finish/Payload 并宣称 root 执行，不成立。保留 DFRoot C/KO 原文时，仅当 ELF 内部产生一个可验证 rooted handoff 才可接入 common Finish；否则需要获批增加 root-context handoff/bootstrap 接线，或取消 43284 common Finish 目标。文件 allowlist 仅证明字节传输，不证明 root 执行。
- 路由回归：`./gradlew :app:testDebugUnitTest --tests com.ghostlock.app.backend.core.ExecutableProcessRoutingTest --no-daemon --console=plain` 成功；XML `tests=5, failures=0, errors=0, skipped=0`。该测试验证执行器选择，不验证 root 权限或 Finish。
- 用户最近答复只裁定 Shizuku 为可选执行器（授权走 Shizuku，否则 App）；未裁定 43284 rooted Finish 是否允许 source delta。当前部署门仍开，且不运行生产漏洞 ELF。
- 设计复核记录：2026-10-29 03:49 America/Toronto · HEAD `b119085915748a940740d7ce919b5a30ed8e9346` · 总判“阻断” · 阻断 2 项（43284 root-context 接线、上游 C/KO 原文与 common Finish 的互斥）· 重要 2 项（staging 执行身份、Flow finish 终态合同）。


### 43499-Glued common Finish 桥接设计复核（2026-10-29）

- design-critic 对当前 rev3 设计作只读复核；范围仅 43499 Glue，未运行 ELF、未修改生产源码。最小候选数据流：`App Flow → 单一 launcher → launcher-owned runDir/staging → 只读 `.ghostlock_root.sh` → unchanged upstream 43499 ELF/victim 同路径 read+exec → rooted victim 执行完整 common Finish/Payload → terminal/log EOF/cleanup`。此设计不依赖 43284 post-exit Finish 决策。
- 阻断 1：`ops.cpp:226-431` 的 bootstrap 与 `FinishScript.kt:31-73` 未逐项等价；差异涵盖 ksud 查找/fallback、KMI、iomem 原子缓存、debug dump、checkreqprot、SELinux policy retry、safe mode、late-load/module wait 与失败路径。
- 阻断 2：native `profile.safe_mode` 与 Kotlin `FinishState.safeMode` 是双权威；43499 Glue 必须以 profile 值为执行唯一权威。
- 阻断 3：上游 bootstrap 已负责 KernelSU late-load，common `RootManagerPayload` 再执行会重复；bootstrap 必须是唯一 late-load owner，common tail 不重复执行。
- 阻断 4：detached `fork()+setsid()` 后代不等于 launcher direct child terminal；需要状态机覆盖 `STAGED → ELF_RUNNING → VICTIM_HANDOFF → FINISH_RUNNING → FINISH_TERMINAL → LOG_EOF → CLEANUP`，取消/断连/EOF 未确认均保留证据。
- 设计复核时的重要项快照（03:53）：两 launcher 尚未设置/验证 `GHOSTLOCK_HOME/TMPDIR/GHOSTLOCK_KSU_LOG`；随后已由 2026-10-29 env fixture 批次验证三键在 App 与 Shizuku 子进程中实际可见。只读脚本的 staging 路径、hash/inode/owner/mode 与 victim handoff 仍未证明；upstream LOG/debug/marker 与 common writer 的 owner、容量与清理规则仍未统一。
- 最小逐文件设计：`Flow43499Glued.kt` 生成并校验 profile 与 finish/payload bytes；`ExecutableBackendProcess.kt` 和 `ExecutableShizukuHost.kt` 各自 staging 到 launcher-owned runDir 并注入 env；`FinishScript.kt` 生成 parity bootstrap + common tail；`PayloadRequests.kt` 排除重复 KernelSU late-load；相关 Kotlin/Android tests 验证 parity、两路同字节、hash/inode/mode、终态与取消。`ops.cpp` 和 `victim_process.cpp` 保持逐字节不变。
- 用户批准门更新：Glue-only disk staging 已由用户原始需求明确授权；Shizuku/App 双 launcher 也已明确裁定。剩余为技术设计/验证门：`ops.cpp:226-431` 完整 bootstrap parity、profile.safe_mode 单一权威、KernelSU late-load 唯一 owner、同路径只读 handoff 身份、终态/EOF/取消/清理；它们不得以“落盘已批准”代替验证。
- 设计复核记录：2026-10-29 03:53 America/Toronto · HEAD `b119085915748a940740d7ce919b5a30ed8e9346` · 总判“阻断” · 阻断 4 项（parity、safe_mode 权威、late-load owner、detached child terminal）· 重要 3 项（双 launcher env/staging、inode identity、log/debug/marker owner）。

#### 43499 bootstrap 与 common Finish 逐项对照（实现前设计表）

| 上游行为 | 当前位置 | common Finish 当前差异 | 必须收敛 |
|---|---|---|---|
| 参数/日志/UID gate：HOME、LOG、SAFE_MODE、KSUD；非 root 即 exit 1 | `ops.cpp:236-269` | `FinishScript.kt:23-31` 只定义 LOG/RUN_ID；无 root gate、HOME、SAFE_MODE、KSUD 的上游绑定 | runDir/env 固定后由 script 读取同一 run 输入；root gate 与退出码必须保真 |
| KSUD 多路径发现及 fallback | `ops.cpp:246-263` | `PayloadRequests.kt:31-55` 用 UI manager spec 自行发现并 late-load，搜索/fallback 列表不完全相同 | 由上游 bootstrap 实现成为唯一 manager/late-load owner；common tail 不重复执行 |
| iomem 原子缓存 | `ops.cpp:270-279` | common script 无 iomem 缓存步骤 | 保留 `.new → validate → atomic rename` 与失败清理语义，确定文件 owner/mode |
| debug dump 与 EXIT trap | `ops.cpp:280-308,339` | common script 无 pstore/kernel-info/dmesg/iomem dump 或等价 trap | debug 目录来自同一 profile/run；异常终止也必须按上游 trap 收尾，失败不得覆盖 exploit terminal |
| checkreqprot 写 0、KMI 从 uname 解析 | `ops.cpp:309-320` | common script 无 checkreqprot/KMI gate | 输出字节与执行次序保持上游语义；无 KMI 时保留原失败路径 |
| safe mode 禁用全部已安装 modules | `ops.cpp:321-333,417` | `FinishState.safeMode` 当前给管理器参数 `--safe-mode`；与 `profile.safe_mode` 不等价且来源重复 | 43499 以 profile.safe_mode 为唯一权威；具体 module disable 语义按上游实现 |
| SELinux policy header 验证、netlink bits fixup、有限重试及 status 变化判定 | `ops.cpp:334-380` | `FinishScript.kt:47-50` 只可选 `setenforce 1` | policy repair 步骤/超时/失败状态逐项保留；区分“fixup failed 但流程继续”的既有结果 |
| KernelSU already-loaded 判定、ksud late-load、模块等待、条件恢复 enforcing | `ops.cpp:381-414` | `PayloadRequests.kt:28-55` 总会构造 root-manager late-load | 上游 bootstrap 唯一执行一次；common payload tail 仅追加用户配置的非重复 payload |
| common payload 顺序、restore SELinux、soft reboot、marker、最终退出码 | `FinishScript.kt:42-64`; `PayloadRequests.kt:128-157` | common writer 有 marker 与状态记录，但结尾没有显式 `exit "$STATUS"`；upstream bootstrap 的状态规则不同 | 明确 bootstrap→user payload→restore/reboot→marker→exit 顺序；执行码、marker 内容与 log terminal 一致 |
| 同路径只读执行桥 | `ops.cpp:229-234`; `victim_process.cpp:142-153` | 双 launcher 的三个路径环境键已由 safe fixture 实测；固定 `.ghostlock_root.sh` 尚未 staging/seal，hash/inode/owner/mode 与 victim handoff 未证明 | 每路 launcher 以本地 runDir 固定同名、只读、hash/inode 可核对的 script；只验证 fixture，不启动生产 ELF |

#### 复核补充：输入、状态与文件合同

- Profile safe-mode 的现有数据路径：`NativeProfileDocument.safeMode` 是 GLK1 `meta.safe_mode` 的序列化权威；`Profile` 目前没有对应便捷属性；`Glued43499ProfileCodec.encode()` 计算 `typed` 后只返回 `Resolved43499Profile(release, sourceText, binary)`，未暴露 safeMode；`Flow43499Glued` 也未把它传给 script generator。common `FinishState.safeMode` 仍独立控制 `RootManagerPayload` 的 `--safe-mode` 参数。设计收敛必须明确单一值源及 UI toggle 的显示/编辑语义，不能靠默认为 false 消除分歧。
- Script materialization 也属于 parity：`ops.cpp:229-234,420-431` 对目标文件执行 `O_WRONLY|O_CREAT|O_TRUNC`，检查格式化长度/单次 write，再 `chmod 0755`；只读预置桥必须定义预期 open errno、失败日志、同路径 handoff 与空/损坏/替换文件拒绝，不能只证明一般 `O_TRUNC` 会失败。
- 状态与文件属性合同待补：上游 `LOG` 初始覆盖写，而 common `FinishScript` 使用 `exec 3>>LOG` 追加；两边的 `GHOSTLOCK_KSU_LOG`、debug dir、iomem/temp 文件 owner/mode、容量与清理需定义单一 writer。KMI、bootstrap、payload、restore/reboot、marker 写入的错误优先级和最终 `exit "$STATUS"` 必须显式定义；marker 或 debug 失败不得掩盖 bootstrap terminal。
- 生命周期门：状态机需覆盖 root script 的 detached child 终态、run.log EOF、cancel、Shizuku binder death 和重试/重复 destroy；任何终态未确认时保留 runDir 与证据。

### 用户需求覆盖与 43499-only 实施边界（2026-10-29）

- **已授权的行为变更：** Glue-only launcher 可把每次运行的 Finish/Payload 脚本及输入落到该 launcher 自己拥有的 runDir；App 路径只用 App-private runDir，Shizuku 路径只用 shell-owned runDir，绝不跨进程复用绝对路径。该例外不改变 `payload-finish-chain-plan.md` 对既有 LKM/legacy 流程的 root 零落盘约束。
- **唯一执行位置：** 43499 不由 launcher UID 执行 Finish。App/Shizuku launcher 只 stage 文件并启动 ELF；原版 `victim_process.cpp:142-153` 创建的 rooted detached worker 才按 `GHOSTLOCK_HOME/.ghostlock_root.sh` 打开并执行。`ops.cpp:229-234` 的 `O_TRUNC` 失败后继续写入/执行路径必须保持；脚本必须预先创建、只读封存，并在 ELF/victim handoff 完成前保持原路径及 inode。
- **环境输入闭包：** 两个 launcher 只注入 `GHOSTLOCK_HOME`、`TMPDIR`、`GHOSTLOCK_KSU_LOG`；前两者指向本次 runDir，KSU log 指向该目录的独立文件，不能与 launcher 的 stdout/stderr `run.log` 共写。未设置 `GHOSTLOCK_KSUD` 时保留 `runtime_config.cpp` 的上游搜索/fallback。Profile 与 Finish/Payload bytes 按两个 launcher 路径做 hash 对拍。
- **脚本结构：** 只允许一个 native 固定入口 `.ghostlock_root.sh`；该文件先复现 `ops.cpp:236-431` 的 root UID gate、KSUD 搜索、iomem/debug、checkreqprot/KMI、profile safe mode、SELinux policy 修复/重试、KernelSU already-loaded/late-load/module wait、trap 与失败退出；仅在 upstream bootstrap 允许继续的路径后调用 common Finish/Payload tail。kernel manager late-load 只由此 bootstrap 执行一次，`RootManagerPayload` 对这一次 KernelSU bootstrap 明确 no-op，其他 manager/payload 行为须单独保留或 fail-closed，不能隐式重复 late-load。
- **safe mode 唯一值：** 43499 执行值取解析后 GLK1 `meta.safe_mode`，native profile 与生成脚本必须逐字一致；Finish UI Safe mode 不再作为第二个独立输入。UI 显示/编辑如何映射到 profile 的 `safe_mode`，在进入代码前由 design-critic 复核并保留现有 UI 控件布局。
- **终态与日志：** 唯一外部 Finish log 为 `GHOSTLOCK_KSU_LOG`；每 run 的脚本嵌入已校验 run id，并在 EXIT trap 写入 `finish-terminal:<runId>:<status>`，launcher 只把同 runId terminal + ELF terminal + stdout EOF 同时成立视为可清理。上游 early `exit` 必须触发 trap；软重启请求、debug dump、marker 写入错误不得覆盖已产生的 bootstrap terminal。取消、binder death、EOF 缺失或状态不确定时保留 runDir/log，不伪报成功，也不清理仍被 detached worker 使用的对象。
- **文件身份：** staging 必须使用固定 basename、拒绝 symlink/non-regular file、以 0444 seal `.ghostlock_root.sh`，记录 stage 后的 SHA-256/dev/inode/owner/mode；`O_TRUNC` 失败 fixture 与 victim 同路径 `O_RDONLY`/exec fixture 对拍同一对象。App/Shizuku runDir 分别检查 owner/mode，并以第二次替换/空文件/截断/partial-write 测试覆盖拒绝路径。
- **逐文件候选与验证：** `Glued43499ProfileCodec.kt` 暴露解析后 `safeMode`；`Flow43499Glued.kt` 从现有 `FinishConfig.state` 构造 per-run Finish/Payload 内容；`FinishScript.kt`/`PayloadRequests.kt` 添加 Glue 专用 bootstrap/tail 生成入口且保持 legacy 默认行为；`ExecutableBackendProcess.kt`/`ExecutableShizukuHost.kt` 增加双路隔离 staging、三个允许环境键与 finish-log terminal 等待；相关 JVM/Android tests 逐项对照 `ops.cpp:226-431`、App/Shizuku 同字节、失败状态、单 late-load、脚本对象身份、cancel/binder death/EOF/清理。`ops.cpp`、`victim_process.cpp`、43284 C/KO 及既有 LKM 代码保持 byte-identical。
- **43284 非本批隐式扩展：** DFRoot `bootstrap.c:196-234` 不读取 `GHOSTLOCK_HOME`/common Finish，且由其固定 prefs/KSUD 路径决定自身 bootstrap；launcher 的 App/shell UID 不能替代 rooted Finish。此问题等待单独裁定，不因 43499 的 Glue-only staging 授权而自动批准 43284 post-exit root handoff。

### 2026-10-29 双 launcher 环境与 AVD UI 批次结果

- 用户裁定：Shizuku 不是必需前提；每次 run 在创建 child 前检查 binder 可达性与授权，两者都满足时只选 Shizuku，否则只选 App 进程权限。选定 Shizuku 后若启动/连接结果不确定，不回退启动第二个 child。App-private 与 shell-owned runDir 分离。
- `ExecutableRunEnvironment.variables()` 对 `43499-Glued` 只生成 `GHOSTLOCK_HOME`、`TMPDIR`、`GHOSTLOCK_KSU_LOG` 三项；对 `43284Glued` 返回空 map。App 与 Shizuku `ProcessBuilder` 共用映射，run.log 与 KSU log 分离。该批未执行生产 ELF。
- 验证：`./gradlew :app:testDebugUnitTest :app:assembleDebug :app:assembleDebugAndroidTest --no-daemon --console=plain` → `BUILD SUCCESSFUL in 41s`, 84 tasks (12 executed, 72 up-to-date); XML 总计 424 tests, 0 failures/errors/skipped。环境专测 2/0，路由专测 5/0。
- 部署：APK `build/app/outputs/apk/debug/GhostLock-v2.0.12(1342)-arm64-v8a-debug.apk` 已安装到 A301SO 与 `emulator-5554`；`com.ghostlock.app` 两端 `2.0.12 (1342)`，base.apk 与构建产物 SHA-256 均为 `720f7100b6bb3ee0a885a5fc13b8c78a6c731f1cc16cdb8ff4c2d78548b71672`。
- AVD UI 证据在 `build/deployment-audit/AVD-backend-selector-20261029.png`、`AVD-43499-card-ui-20261029.png` 和 `AVD-paths-ui-20261029.png`：选择器显示两个 Glued backend 可选，三个原版后端与 Custom 灰显；43499 card 显示内置 profile 列表与 Import `.conf`，43284 card 无 profile/import 控件。
- 当前不等于可用攻击版本：43499 Finish parity、safe_mode 单一权威、late-load owner、固定 `.ghostlock_root.sh` staging/seal 与 detached worker terminal/EOF/cleanup 仍未闭合；43284 common Finish/root handoff 仍待独立设计裁定。

#### 双 launcher 环境变量 safe fixture 复验（2026-10-29）

- 修改仅限安全 no-op fixture 与 instrumentation assertion；fixture 读取 stdin 并报告固定 marker/参数字节数，43499 编译变体额外报告三个路径变量，不调用上游漏洞逻辑。
- 命令：`./gradlew :app:testDebugUnitTest :app:assembleDebug :app:assembleDebugAndroidTest --no-daemon --console=plain`；结果 `BUILD SUCCESSFUL in 41s`，84 actionable tasks（15 executed，69 up-to-date），XML 汇总 424 tests / 0 failures / 0 errors / 0 skipped。
- AVD App route：`adb -s emulator-5554 shell am instrument -w -e expected_route APP -e class com.ghostlock.app.backend.core.GluedExecFixtureSmokeTest com.ghostlock.app.test/androidx.test.runner.AndroidJUnitRunner` → `OK (2 tests)`，exit 0。Logcat 证明 `GHOSTLOCK_HOME == TMPDIR == /data/user/0/com.ghostlock.app/files/ghostlock-glue/43499-Glued/<run-id>`，`GHOSTLOCK_KSU_LOG=<home>/.ghostlock_ksu.log`。
- A301SO auto route：`adb -s adb-QV770MFGJ1-XxNlpb._adb-tls-connect._tcp shell am instrument -w -e class com.ghostlock.app.backend.core.GluedExecFixtureSmokeTest#dispatches43499FlowThroughTheSelectedLauncher com.ghostlock.app.test/androidx.test.runner.AndroidJUnitRunner` → `OK (1 test)`，exit 0；运行时实际 route 为 `SHIZUKU`，home/tmpdir 指向 shell-owned `/data/local/tmp/ghostlock-glue/CVE_2026_43499_GLUED/<run-id>`，KSU log 为该 runDir 下 `.ghostlock_ksu.log`。
- 结束态：fixture 与 test APK 均移除/覆盖；A301SO 与 AVD 恢复安装当前 source APK `2.0.12 (1342)`，两端 `base.apk` 均与构建产物 SHA-256 `8cdb0e127871bb271af3850a378e4860fd3db00ea8ef9225bd575811440562ea` 一致；test package 不存在，未见 fixture/Glued 进程。
- 该证据关闭“环境变量未进入实际子进程”项；不关闭 `.ghostlock_root.sh` staging/seal、detached-worker terminal/EOF/cancel cleanup、Finish parity/safe_mode/late-load owner，也未运行生产 ELF。


### A301SO / AVD 部署前依赖复核（2026-10-29）

- 当前路由回归：`./gradlew :app:testDebugUnitTest --tests 'com.ghostlock.app.backend.core.ExecutableProcessRoutingTest' --no-daemon --console=plain` → `BUILD SUCCESSFUL in 5s`；`build/app/test-results/testDebugUnitTest/TEST-com.ghostlock.app.backend.core.ExecutableProcessRoutingTest.xml` 为 5 tests / 0 failures / 0 errors / 0 skipped。该项只证明 binder+permission 路由规则与单 launcher owner，不证明 Finish 或 root handoff。
- 设备包清单：`adb -s adb-QV770MFGJ1-XxNlpb._adb-tls-connect._tcp shell pm list packages` 包含 `df.root`、`me.weishu.kernelsu`、`com.ghostlock.app`；`adb -s emulator-5554 shell pm list packages` 包含 `me.weishu.kernelsu`、`com.ghostlock.app`，没有 `df.root`。
- 同路径读取探测：A301SO shell 查看 `/data/user_de/0/df.root/shared_prefs/dfroot.xml` 返回 `Permission denied`；AVD 返回 `No such file or directory`。因此 A301SO 安装 DFRoot 包只证明包存在，不证明 prefs 内容、DFRoot 可运行状态或 43284 bootstrap 成功；AVD 不满足 DFRoot 固定 prefs 路径存在这一先决条件。
- 这项复核没有启动 Glued ELF；43284 common Finish/root handoff 仍需用户裁定，不能以包存在或 `/dev/dfm6` 等 upstream marker 代替 Finish terminal。


### 用户补充确认：双 launcher 为择一执行器（2026-10-29）

- 用户确认 Shizuku 不是运行前提；Shizuku launcher 与 App launcher 并列可用。每次 run 在创建 child 前检查 Shizuku binder 与授权：两者均可用时只由 Shizuku 创建该次进程，否则只由 App 创建；这表示并列后端选择，不是同时启动两个 child。
- 已选 launcher 若启动/连接结果不确定，不自动切换另一 launcher，以免同一 Flow 产生重复执行。当前 `ExecutableProcessRouting` 已按该规则实现；基线与改后均执行同一回归测试，改后验证见本节关联 transaction evidence。
