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
- 本次主机 Kotlin 编译不等于 Shizuku exec 接线或真机部署证据；它们仍是独立验证项。当前未完成项：执行器仍用 app `ProcessBuilder`，未接 Shizuku UserService；43284 上游 CLI 参数尚未映射（Flow 当前传空参数）；因此不得标记已部署/可用版本。

## 选择器与配置纠偏（2026-10-29）

- 根因：旧 `ExecGlueSpec` 把 43499 的 `titleRes` 和 `Ready` 继承给两个 Glued backend，导致 43284 也显示成 43499；旧 `BackendSelection` 不校验 disabled 状态，因而旧 43499、两个 Glued backend 和 43284 均可选。现已移除该抽象。
- 选择权收敛到 `Backends.isSelectable(spec)`：通过 `===` 只接受 `BackendCve43499Glued` 与 `BackendCve43284Glued` 两个 singleton，且二者显式声明 `selectable=true`。不依赖 `equals/hashCode`，避免动态 Lua spec 冒充内置对象。Material、Miuix、PathViewModel、会话恢复与 BackendRunGate 共用该判据；原版 43499/43284/98260、内置 Custom Lua 与动态 Lua 即使误声明 `true` 仍保持灰显/不可选。
- `43499-Glued` 的 backend readiness 只依赖 card 选择并验证通过的 profile；运行入口不会再把原版 JNI 43499 的“物理输入未接入”原因冒充为 Glued 的配置要求。profile card 从 `kernel_profiles/index.conf` 提供内置列表并支持 SAF 导入 `.conf`。
- 目标 profile 索引、profile 资产和 `LegacyProfileConverter.kt` 均核对为当前 `origin/main`（`437b3b257462e17d395cfd2cd8fb81536d6c6358`）的逐字节副本；当前工作树中旧 `app/src/main/assets/profile/` 有 146 个删除项，主资产配置唯一来源为 `kernel_profiles/`。debug source set 的实验配置仍独立保留，Glued 43499 不读取它。
- 代码库中仅保留两个 backend 的直接声明与 `ExecutableBackendProcess` 公共执行助手，不另设 Glued 继承层。

## 最终部署批次设计 rev1（已被 rev2 取代；2026-10-28）

### 现状与目标

当前 APK 虽已打包并安装到 A301SO，但 `ExecutableBackendProcess` 仍在 app UID 直接 `ProcessBuilder.start()`；`ShizukuHostFlowExecutor` 仍返回 `remote-pending`；43284 Flow 仍传空 argv。安装成功不等于启动链完成。本批把 Flow 的单一 exec step 接到已有共用 Shizuku UserService，并把 43284 所需 IPsec 参数在运行时创建和释放。

### 选定数据流

`App Flow (SequentialFlow, one exec step)` → `ExecutableBackendProcess`（本地 run artifact/log 落盘）→ `ShizukuBackendClient.prepare(Bundle,payload)/start/state/readLog/verify` → 现有 `GhostlockUserService` → `ShizukuBackendHosts` registry → Glued process host → `ProcessBuilder` 在 shell UserService 中 exec 原版 ELF。App 进程不再启动攻击 ELF，不配置隐式 App UID 回退。

- 43499：App 保存所选 `.conf` 和 profile binary 到本地 run 目录；Bundle 传 ELF 名称、固定 argv、文件清单及 `.conf` 字节；payload 传 profile binary；host 只把已校验的 built-in/import profile 和 binary 写入独立 `/data/local/tmp/ghostlock-app/<run-id>`，随后以 `--ghostlock-app-call` exec，并按原版无长度帧 stdin 约定写 binary。上游 `main.cpp` 与 C/C++ 文件保持不变。
- 43284：App Flow 只选后端并提交空 payload；host 使用 UserService `Context` 创建 DFRoot `IpSecManager` runtime，按原版 `exp.c` 的 CLI 解析器生成 `--encap-port/--sender-port/--spi/--aes-key` argv，在 ELF 退出、准备失败或 host destroy 时关闭 transform、SPI 与 encapsulation socket。密钥只在内存参数中存活，不写入日志/配置/文档。
- host stdout/stderr 重定向到 run log 文件；共用 UserService 日志泵按偏移回传，避免 pipe 读取在 native 时序窗口造成 backpressure。app 收到的行同时写入 app 私有 run log。terminal evidence 由 exit status/verify 产生。
- `destroy()` 不增加 kill/signal AIDL；Flow 只在 terminal 后 `disconnect(remove=true)`。失败、binder disconnect、取消和 destroy 的资源终结顺序纳入实现与 fake-host 测试。

### 改动范围与保留

- 修改：`ExecutableBackendProcess.kt`、`FlowLaunchParent.kt`、`GhostlockUserService.kt`、`ShizukuBackendHost.kt`、`ShizukuBackendClient.kt`；在 `43499glued/`、`43284glued/` 增加后端 host adapter/runtime；增加共享 Shizuku host stable IDs；新增 Flow/host contract JVM tests；同步本计划与 `full-process-uml.md` exec 流程图。
- 保留：`SequentialFlow` 各一个 exec step；backend 选择 allowlist；43499 card/上游 profile 与转换器；通用 Finish/Payload 文件业务；上游 C/C++ 原文与 ELF entrypoint；原版后端和 Lua disabled；无 App 进程回退、无第二个 UserService/AIDL、无新 native route。
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
- host 写入 shell-owned `/data/local/tmp/ghostlock-app/<UUID>/` 下固定文件名；`GHOSTLOCK_HOME` 指向本次 run dir。仅使用声明过的环境键：`GHOSTLOCK_HOME`、`TMPDIR`、`GHOSTLOCK_KSU_LOG`、`GHOSTLOCK_KSUD`。App-side SAF URI 不传 host；所选 conf 与脚本内容由 App 读取后放入受限 invocation。
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
- 阻断：`payload-finish-chain-plan.md:26-32` 要求 root 零落盘、FinishScript 只由 rooted victim 执行；Glued 新需求要求 App/Shizuku launcher 预置磁盘脚本。已向用户请求知情裁定：是否以新需求覆盖旧约束，并在同批登记文件 owner/mode/hash/清理与技术债；收到明确裁定前不写该生产桥接。
- `O_TRUNC` 狭义语义已有实证：`ops.cpp:226-234` 失败分支只返回；`cve_2026_43499_backend.cpp:453-458` 调用后继续 setup；`victim_process.cpp:142-153` 对同一路径只读打开并执行。AVD 与 A301SO 的 App UID、shell UID safe fixture 均得 `TRUNCATE_EXIT=1`、`CONTENT_UNCHANGED=yes`、`SCRIPT_EXEC_EXIT=0`、`MARKER=fixture-ok`，fixture 清理后文件不存在。证据：`docs/analysis/device-gates/20261029-0126-43499-readonly-script-bridge-fixture.md`。design-critic 关闭“写失败是否阻断静态控制流/只读执行机制”项；实际 Glued 双 launcher 的 runDir 路径、owner/mode/inode 与 native handoff 集成仍未验证。
- 其余阻断：① native root script 与共同 `RootManagerPayload` 可能重复 late-load，执行 owner 未定；② upstream `profile.safe_mode` 与 `FinishState.safeMode` 缺单一权威；③ App-private 与 Shizuku shell 的 payload staging、可读性、校验和清理未定；④ upstream `LOG`/debug dump、共同 log/marker 的映射和容量未定；⑤ detached `fork()+setsid()` worker 的完成/取消/目录清理所有权未定。
- 本复核未修改上游 C/C++ 或生产 Kotlin/native 源；不运行漏洞 ELF。只有关闭上述设计项、经 design-critic 复核和用户批准后，才实现并执行双 launcher safe fixture 集成门。

### 批次 6 设计：Shizuku 同 tag 快速重绑定竞态（2026-10-29）

- 现象：A301SO 最新源码的 no-op fixture 全类运行中，43284Glued 第一次通过；紧随其后的 43499-Glued 在 5.078 s 后返回 `exec:shizuku:prepare:-8`。Shizuku server log 显示第二个 service record 在约 0.5 s 内收到 UserService binder，但 app 未收到 `onServiceConnected`；随后 `lastReason()` 再 bind 时 server 显示 `Found existing service record`。这证明失败发生在绑定回调/生命周期，不是 ELF staging 或 child exec。
- 机制证据：Shizuku API 13.1.5 的 `Shizuku.unbindUserService(remove=true)` 不同步清空 `ShizukuServiceConnections`；其 `ShizukuServiceConnection.died()` 把 ServiceConnection 回调、connections 清理及缓存移除投递到主线程。当前客户端在 unbind 返回后立即清理本地字段，下一 run 可在 Shizuku 主线程完成该死亡回调之前复用同 tag 缓存连接。
- 行为差异：保持稳定 tag 与每 run 清除 UserService 的现有所有权；`disconnect(remove=true)` 在已建立绑定时等待 `onServiceDisconnected` 回调之后的一个主线程队列轮次，确认 Shizuku callback/cache cleanup 已退出，再允许后续 run bind。等待有界；无 binder 或未建立连接时不等待。绑定失败原因保存在 client 本地，不通过 `lastReason()` 触发第二次隐式 bind。
- 影响文件：`app/src/main/kotlin/com/ghostlock/app/shizuku/ShizukuBackendClient.kt`；`docs/development/full-process-uml.md` 的 Glued 双 launcher sequence；本计划。
- 验证：先跑 `ExecutableProcessRoutingTest` 与全量 `:app:testDebugUnitTest`；重建 debug/APK test；A301SO 用仅含固定输出/只读 stdin safe stub 的 fixture 连续运行 43284→43499 全类至少 10 次，逐次要求 `OK (2 tests)` 且 `route=SHIZUKU`；保存失败前后 logcat；最后重装基线生产 APK、卸载 instrumentation APK，并核对 installed hash 与进程列表。不得运行上游漏洞 ELF。
- 原因判定仍属假设，直到上述 fix 后的设备矩阵通过；若死亡回调等待超时，保留具名诊断并停止后续 Shizuku rebind，不静默开第二个 launcher。
