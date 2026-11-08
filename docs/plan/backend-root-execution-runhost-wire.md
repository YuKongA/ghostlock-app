# RunHost typed wire 与状态机（backend-root-execution-unification 附件 · L · 2026-11-04）

<a id="p-run-ipc-001"></a>

> **附件代号**：`P-RUN-IPC-001`（产品－执行编排－进程通信合同），从属 [P-RUN-X-001：执行统一计划](backend-root-execution-unification-plan.md#p-run-x-001)。本页章节号仅标 wire 定义；决策/批次使用[主计划语义代号](backend-root-execution-unification-plan.md#plan-semantic-identifiers)，原批准与旧 D/R 引用按该表兼容，不新增授权。

> 状态：**已随 [九项语义决策（原 D1–D9）](backend-root-execution-unification-plan.md#p-run-x-001-decisions) 获批；2026-11-04 用户单独批准进入 [P-RUN-X-001/批次01：execution spine（原 R3）](backend-root-execution-unification-plan.md#p-run-x-001-batch-01)，本附件是 production typed wire/state implementation authority**。
>
> 本附件只冻结 App/Shizuku RunHost 的跨进程字段、范围、方法、状态、并发和稳定错误码；产品目标、owner、批次与成功语义以主计划为准。出现冲突时主计划优先，本附件不得扩展 scope。
>
> 2026-11-08 的 `P-RUN-X-001/批次02` 只重新归属 Kotlin owner：纯合同在 `core/runhost`，client/codec/host adapter 在 `data/runhost`，App framework entry 为 `runhost/AppRunHostService`。本页 AIDL method/field/integer、状态与错误优先级未变。

## 1. 不变量

1. App 与 Shizuku 使用同一 wire；执行域不同，不产生两套业务合同。
2. request 只含 bounded typed fields 与 read-only `ParcelFileDescriptor`；禁止 reflection symbol、class/method name、arbitrary `Bundle`、registry digest、自由输入 path。
3. 同一 RunHost 实例同一时刻只拥有一个 run；一个实例不用来串行复用多个 run。
4. `runId + generation` 是 callback/snapshot/control 的复合身份；任一不匹配均 fail-closed。
5. 所有控制方法同步返回稳定 `RunHostCode`；log/terminal callback 是 oneway、只读、可丢日志但不可丢 terminal snapshot。
6. terminal snapshot 一经发布不可变；日志文本不参与状态转移或产品成功。
7. `destroy()` 不得成为不可中止 backend 的 kill 通道；launcher 只有在 `requestClose` 接受后才调用 framework destroy/unbind。

## 2. 标识 grammar 与 hard limits

| 字段 | grammar / range | owner |
|---|---|---|
| `runId` | exactly 32 lowercase hex：`^[0-9a-f]{32}$` | application `RunInputFreezer`；每次 run 新随机值，不复用 |
| `generation` | signed 64-bit `1..Long.MAX_VALUE` | application `LogHub`；同一 process 单调递增，溢出即拒绝新 run |
| `backendKey` | `^[A-Za-z0-9][A-Za-z0-9._:-]{0,95}$`，且必须 exact 命中 immutable registry | `BackendDescriptor.key` |
| `logicalId` / item id | `^[a-z][a-z0-9._-]{0,63}$`，同一 request 内全局唯一 | freezer；renderer 不重命名 |
| SHA-256 | exactly 64 lowercase hex | application 对输入 artifact 计算一次；RunHost staging 后对 staged fd 重算 |

| 预算 | 上限 | 拒绝码 |
|---|---:|---|
| request Parcel（不含 fd 内容） | 256 KiB | `LIMIT_EXCEEDED` |
| input artifacts | 32 | `LIMIT_EXCEEDED` |
| 单 artifact | 512 MiB | `LIMIT_EXCEEDED` |
| 全部 artifact bytes | 1 GiB | `LIMIT_EXCEEDED` |
| Payload items | `0..64`；capability-specific 下限见 §3.1 | `LIMIT_EXCEEDED` |
| Finish items | `0..32`；capability-specific 下限见 §3.1 | `LIMIT_EXCEEDED` |
| 单 item args | 32 | `LIMIT_EXCEEDED` |
| 单 arg UTF-8 bytes | 4 KiB | `LIMIT_EXCEEDED` |
| callback log message | 8 KiB printable ASCII + §3.9 SGR 子集 | 超出或非法时不发送，`droppedLogs` 加一；不截 terminal |
| serialized callback log | 12 KiB | 该 log 不发送，`droppedLogs` 加一；不改变产品 terminal |
| cumulative callback log count | 无人为条数上限；sequence 为正 `Long` | 仅单条/Parcel非法或 `Long.MAX_VALUE` 耗尽时拒绝该log并增加 `droppedLogs` |
| snapshot Parcel | 256 KiB | 丢弃 oversized candidate，发布 bounded minimal snapshot：`lastCode=23`、未完成 result=`UNKNOWN/reason=13`、`terminal=5`；不得把 code/reason 名写进 terminal |
| root result file | 256 KiB | parser 返回 `MALFORMED_RESULT/UNKNOWN`；不作部分解析 |

所有 `0`、`-1`、上限、上限+1、空值、重复 ID、非法 UTF-8/非 ASCII log 都进入 [P-RUN-X-001/批次01：execution spine](backend-root-execution-unification-plan.md#p-run-x-001-batch-01) / [P-LOG-X-001/批次03：LogConnector与顺序](backend-root-execution-unification-plan.md#p-log-x-001-batch-03) 极端值矩阵。

## 3. Wire DTO

### 3.1 `RunHostRequest`

| 字段 | 类型 | 约束 |
|---|---|---|
| `runId` | `String` | §2 grammar |
| `generation` | `Long` | §2 range |
| `backendKey` | `String` | §2 grammar + registry exact lookup |
| `profileArtifactId` | `String` | 指向 artifacts 中唯一 canonical profile snapshot；不得为空 |
| `payloadItems` | `List<PayloadItemParcel>` | 有序、ID 唯一、§2 count |
| `finishItems` | `List<FinishItemParcel>` | 有序、ID 唯一、§2 count |
| `artifacts` | `List<RunInputArtifactParcel>` | logical ID 全局唯一；PFD read-only；禁止目录、socket、pipe、symlink |

request 不携带 schema/digest handshake，也不携带 `executorKind`。`BackendDescriptor.executionDomain` 是期望域唯一 authority；RunHost 以自己的 process role/uid/SELinux domain 机械核对，任何不一致固定返回 `EXECUTION_DOMAIN_MISMATCH`。同 APK AIDL 与 generated Parcelable 是 wire authority。profile 内容仍遵守 GLK v3 JSON/JCS/schema authority，RunHost 不接受 legacy profile。

request 也不携带可由 caller 伪造的 run capability。RunHost 在 exact registry lookup 后只从 immutable `BackendDescriptor.runCapability` 取得 §6.6 的 `RunCapability`，并按下表验证 request shape：

| `RunCapability` | Payload | Finish | 结果 |
|---|---|---|---|
| `PRODUCT_ROOT` | `1..64`，且至少一个 `required=true` | `0..32` | 空 Payload、全 optional Payload 或超限均拒绝；空 Finish 合法 |
| `DIAGNOSTIC_ONLY` | 必须空 | 必须空 | 任一产品 Payload/Finish item 都拒绝 |

shape 不匹配固定返回 `INVALID_REQUEST`。因为 capability 只有 registry lookup 后才可知，同一**结构合法但 capability-incompatible** shape 配未知 backend key 时先返回 `UNKNOWN_BACKEND`；命中 backend 后再做 capability shape 检查，顺序见 §5.1。结构本身的 grammar/enum/reference 错误仍按 §5.1 step 2 先返回 `INVALID_REQUEST`。`DIAGNOSTIC_ONLY` 可携带 profile 与 host fixture 所需的 read-only artifact，但这些 artifact 不得通过 Payload/Finish 冒充产品动作。

### 3.2 `RunInputArtifactParcel`

| 字段 | 类型 | 约束 |
|---|---|---|
| `logicalId` | `String` | §2 grammar |
| `kind` | `Int` | `1=PROFILE`、`2=USER_SCRIPT`、`3=EXECUTABLE`、`4=LKM`、`5=DATA` |
| `byteCount` | `Long` | `0..512 MiB`；kind 可另行拒绝 0 |
| `sha256` | `String` | §2 exact lowercase hex |
| `fd` | `ParcelFileDescriptor` | RunHost duplicate 后 own；`fstat` 必须 regular、size exact |

不传目标 path、mode、uid 或 SELinux label；这些由 host-local staging policy 决定。

### 3.3 `PayloadItemParcel`

| 字段 | 类型 | 约束 |
|---|---|---|
| `itemId` | `String` | §2 grammar、Payload 内唯一 |
| `kind` | `Int` | `1=COMMAND`、`2=SCRIPT`、`3=ROOT_MANAGER`、`4=LKM`、`5=DETACHED_PROCESS` |
| `required` | `Boolean` | 默认 true；false 仍记录结果 |
| `artifactId` | nullable `String` | SCRIPT/ROOT_MANAGER/LKM/DETACHED 必须指向 compatible artifact；COMMAND 必须 null |
| `args` | `List<String>` | argv token；不是 shell 拼接字符串；§2 budget |
| `acceptanceKind` | `Int` | 与 kind 允许集合匹配；未知值拒绝 |

COMMAND 正文若超过单 arg 上限，必须作为 USER_SCRIPT artifact；不把任意大脚本塞进 Binder。

`PayloadAcceptanceKind` 固定为：`1=EXIT_ZERO`、`2=DAEMON_ACK`、`3=MODULE_PRESENT`、`4=TAKEOVER_ACK`。Payload 的完整合法组合只有下表；unknown/reserved kind 或 acceptance、其它组合一律 `INVALID_REQUEST`：

| Payload `kind` | `acceptanceKind` | `artifactId` | `args` |
|---|---|---|---|
| `1=COMMAND` | `1=EXIT_ZERO` | 必须 null | `1..32` 个 argv token；第一个 token 是 executable，不接受 shell command string |
| `2=SCRIPT` | `1=EXIT_ZERO` | 必须指向 `kind=USER_SCRIPT` artifact | `0..32` 个 positional arg |
| `3=ROOT_MANAGER` | `2=DAEMON_ACK` | 必须指向 `kind=EXECUTABLE` artifact | `0..32` 个 argv token |
| `4=LKM` | `3=MODULE_PRESENT` | 必须指向 `kind=LKM` artifact | `0..32` 个 module arg |
| `5=DETACHED_PROCESS` | `4=TAKEOVER_ACK` | 必须指向 `kind=EXECUTABLE` 或 `USER_SCRIPT` artifact | `0..32` 个 argv token |

### 3.4 `FinishItemParcel`

| 字段 | 类型 | 约束 |
|---|---|---|
| `itemId` | `String` | §2 grammar、Finish 内唯一且不得与 Payload ID 重复 |
| `kind` | `Int` | 本节下方 `FinishItemKind` 稳定整数；未知/reserved 值拒绝 |
| `severity` | `Int` | `1=NORMAL`、`2=SAFETY_CRITICAL` |
| `alwaysRun` | `Boolean` | Payload fail-fast 后是否仍执行 |
| `artifactId` | nullable `String` | kind 需要文件时 exact 引用 |
| `args` | `List<String>` | §2 budget |

`FinishItemKind` 固定为：`1=COMMAND`、`2=SCRIPT`、`3=REBOOT`。完整合法组合只有下表；新增 Finish 能力必须先改本 wire authority，不能复用 reserved integer：

| Finish `kind` | `artifactId` | `args` | 额外约束 |
|---|---|---|---|
| `1=COMMAND` | 必须 null | `1..32` 个 argv token | 第一个 token 是 executable；不接受 shell command string |
| `2=SCRIPT` | 必须指向 `kind=USER_SCRIPT` artifact | `0..32` 个 positional arg | script 内容只走 PFD artifact |
| `3=REBOOT` | 必须 null | 必须空 | 只允许 `alwaysRun=true`，至多一个且必须为 Finish 最后一项；durable handoff 服从 §7.4 |

所有 Finish kind 都必须显式给出 §3.4 的 `severity`；unknown/reserved kind、artifact kind 不匹配、COMMAND/REBOOT 携带 artifact、REBOOT 带 args、`alwaysRun=false`、重复 REBOOT 或 REBOOT 后仍有 item 都固定 `INVALID_REQUEST`。

### 3.5 `RunHostSnapshot`

| 字段 | 类型 | 语义 |
|---|---|---|
| `runId` / `generation` | identity | 必须与请求一致 |
| `state` | `Int` | §5 `RunHostState` |
| `lastCode` | `Int` | 最近一次稳定 host code；不替代 report |
| `runCapability` | `Int` | §6.6；只从命中的 immutable `BackendDescriptor` 投影，不回读 request |
| `diagnosticOutcome` | `Int` | §6.6；产品 run 固定 `NOT_APPLICABLE` |
| `backendPrimitive` | typed stage parcel | raw-root/primitive fact |
| `scriptDispatch` | typed stage parcel | same-artifact dispatch fact |
| `payloadItems` / `payloadOverall` | typed result parcels | root result protocol解析 |
| `finishItems` / `finishOverall` | typed result parcels | root result protocol解析 |
| `backendCleanup` | typed stage parcel | backend cleanup fact |
| `terminal` | `Int` | 仅 TERMINAL 后非 `NONE` |
| `logSequence` | `Long` | host 最后为 callback delivery attempt 保留的 sequence；从 1 单调递增，至 `Long.MAX_VALUE`；无累计条数上限 |
| `droppedLogs` | `Long` | host 侧 validation、单条/Parcel限制、sequence耗尽或callback delivery日志丢弃数；不参与产品结果 |

snapshot 禁止携带 raw log、任意 evidence map、native pointer、fd 或自由 path。callback由应用侧`RunHostLogConnector`适配为`LogDraft`并动态attach到唯一`LogHub`；LogHub再经raw/processed双流与单dispatcher非阻塞分发到UI、private-file及可选MediaStore独立sink。

### 3.6 `RunHostSnapshotReply`

| 字段 | 类型 | 约束 |
|---|---|---|
| `code` | `Int` | §6 `RunHostCode`；unknown code 由 client 映射 `INTERNAL_CONTRACT_FAILURE` |
| `snapshot` | nullable `RunHostSnapshot` | `code=OK` 时必须非空；其它 code 时必须 null |

### 3.7 `StageResultParcel`

| 字段 | 类型 | 约束 |
|---|---|---|
| `state` | `Int` | §6.2 `StageState` exact value |
| `reason` | `Int` | §6.5 `ResultReason` exact value；unknown value 在 decode 边界改成 `INTERNAL_CONTRACT_FAILURE + UNKNOWN` |
| `exitKind` | `Int` | §6.4；没有 process/native status 时必须 `NONE` |
| `exitValue` | `Int` | 与 `exitKind` 联合校验；不能用负数兼任 sentinel |

`StageResultParcel` 不携带任意 reason string、log、path、pid、uid、地址或 backend-private map。诊断详情只进 log；产品合成只读上述稳定字段。

### 3.8 `ItemResultParcel`

| 字段 | 类型 | 约束 |
|---|---|---|
| `itemId` | `String` | 必须 exact 命中 request 中对应 Payload/Finish item，且只出现一次 |
| `state` | `Int` | §6.3 `ItemState` exact value |
| `reason` | `Int` | §6.5 exact value |
| `exitKind` / `exitValue` | `Int` / `Int` | §6.4；root-script item 禁止 `NATIVE_STATUS` |

snapshot 中 Payload/Finish result list 的顺序必须与 frozen request 完全相同；缺项、重复项、换序、额外项或跨 phase 重用 ID 都映射 `MALFORMED_RESULT/UNKNOWN`，不得只丢坏项后继续合成。

### 3.9 `RunHostLogRecord`

| 字段 | 类型 | 约束 |
|---|---|---|
| `runId` / `generation` | identity | 与 host identity exact 相同 |
| `sequence` | `Long` | 正 `Long`，从1开始逐条连续；gap、重复、倒序均使应用侧connector返回Incomplete，不成为跨来源全局顺序 |
| `elapsedRealtimeMs` | `Long` | `0..Long.MAX_VALUE`，仅保留 source-side 诊断时间；不参与全局排序或 success |
| `origin` | `Int` | `1=KOTLIN`、`2=NATIVE`、`3=SHELL`；unknown 值丢弃 |
| `level` | `Int` | `1=VERBOSE`、`2=DEBUG`、`3=INFO`、`4=WARN`、`5=ERROR`；unknown 值丢弃 |
| `message` | `String` | 最多 8 KiB；单行 printable ASCII，另允许下述 foreground/reset SGR 子集；不参与控制流 |

SGR grammar 固定为 `ESC '[' parameter (';' parameter){0,3} 'm'`：每个 parameter 为 1–3 位十进制，且只能是 `0`、`30..37`、`39`、`90..97`；整个序列最多 16 bytes。禁止 NUL、CR/LF、TAB、光标/擦除/链接/粗体等其它控制序列。整个 log Parcel 不得超过 12 KiB；host 侧验证、预算、parcel 或 callback delivery 失败会丢弃该 record 并计入 host-owned `droppedLogs`。client 对 identity、sequence、enum、message 或 parcel 的独立复验失败会保留此前合法prefix，并把该`RunHostLogConnector`终结为`Incomplete`；它不反向改写 immutable host snapshot。应用client在唯一completion deadline内同时等待typed terminal snapshot、callback terminal marker和connector end；snapshot先到不允许提前关闭transport。immutable terminal snapshot仍是产品authority，不能用log补造terminal。

## 4. AIDL 方法与 transaction 语义

候选接口精确形状：

```aidl
interface IRunHost {
    int setCallback(String runId, long generation, IRunHostCallback callback) = 1;
    int prepare(in RunHostRequest request) = 2;
    int start(String runId, long generation) = 3;
    RunHostSnapshotReply getSnapshot(String runId, long generation) = 4;
    int cancel(String runId, long generation) = 5;
    int requestClose(String runId, long generation) = 6;
    void destroy() = 16777114;
}

oneway interface IRunHostCallback {
    void onLog(in RunHostLogRecord record) = 1;
    void onTerminal(in RunHostSnapshot snapshot) = 2;
}
```

- 除 callback 外全部 synchronous；返回发生在 host mutex 内状态转移完成后，是该调用的 linearization point。
- `RunHostSnapshotReply` 固定为 `code:Int + nullable snapshot`；仅 `code=OK` 时 snapshot 非空，其它 code 时必须 null。transport exception 不伪装成 reply code。
- `onLog/onTerminal` oneway，经单一 serial callback queue 发送；同generation的sequence只是RunHost source内部的连续交付证明。应用侧connector等待terminal marker并检查final sequence/dropped count；跨App/Shizuku/Kotlin/FD source的唯一全局顺序是draft被LogHub raw ingress接受的顺序，source time不触发回溯重排。
- host 的 terminal **publication** 由 compare-and-set 恰好一次写入 durable immutable snapshot；`onTerminal` 对某 callback 的 delivery 是 at-most-once，Binder death/未注册 callback 时可以 0 次。新 callback 不重放 terminal，client 用 `getSnapshot` 读取 authority。
- `destroy()` 明确保留 Shizuku UserService framework 固定 transaction id `16777114`；service 端也按 §5 状态表拒绝/忽略提前 transaction，不能只依赖业务 client 自律。业务 client 只有 `requestClose=OK/ALREADY_APPLIED` 且 host 已进入 DESTROYED 后才调用 framework destroy。
- App client 对应动作是 unbind/stop service；两端仍走相同 `requestClose` 判据。

## 5. Identity、错误优先级、状态机与并发

### 5.1 Identity 建立与确定性错误优先级

- host identity 是 `runId + generation`；`setCallback` 与 `prepare` 都携带完整复合 identity。
- 第一次成功 `setCallback` 可在 NEW 预留 identity；之后 `prepare` 必须相同。若没有 callback，第一次成功 `prepare` 原子建立 identity 与 PREPARED。
- `prepare` 成功后保存 normalized typed fields + artifact metadata（logicalId/kind/byteCount/sha256）的语义指纹；不比较不稳定的 Parcel bytes 或 fd number。
- backend Flow构造与constructor-time native load发生在任何PREPARED提交之前；只有完整`PreparedRun candidate`构造成功后，host才在同一host-lock临界区执行`lifecycle.prepare()`并原子发布`prepared + snapshot`。factory/load异常返回`IO_FAILURE`、删除workspace且lifecycle保持NEW；并发`start()`即使在该临界区内观察到state已转PREPARED，也必须等待同一host lock并取得非空`prepared`。
- accepted `start()`的worker在调用`Flow.run`前把本次host-owned workspace root写入进程`GHOSTLOCK_HOME`；该写入与`Flow.run`位于同一terminal failure boundary。只有accepted running candidate可写，prepare loser、未启动candidate和预览路径不得写；同一host进程不重叠运行两个Flow，因此common handoff只解析当前run的`GHOSTLOCK_HOME/root-script/root-script.sh`。
- host 实例 one-shot，不接受下一 generation 复用。

所有 synchronous 方法按下列顺序返回第一个命中的 code；实现和测试不得改变优先级：

1. Binder/Parcelable 解码失败是 transport exception，不映射业务 code。
2. 参数 grammar、nullability、enum/reference 非法 → `INVALID_REQUEST`；任一 hard limit 超限 → `LIMIT_EXCEEDED`。
3. 已是 DESTROYED → `BAD_STATE`，唯 `requestClose` 返回 `ALREADY_APPLIED`；framework `destroy()` 见状态表。
4. identity 已建立时：`generation < current` → `STALE_GENERATION`；`generation > current` → `BUSY`；generation 相同但 runId 不同 → `RUN_MISMATCH`；相同 identity 的 successful-prepare 语义指纹不同 → `RUN_MISMATCH`。
5. identity 未建立时，除 `setCallback`/`prepare` 外的方法 → `BAD_STATE`；然后检查方法×状态合法性。
6. `prepare` 依次检查 backend key → `UNKNOWN_BACKEND`；命中后按 `BackendDescriptor.runCapability` 检查 §3.1 request shape，不匹配 → `INVALID_REQUEST`；`BackendDescriptor.executionDomain` 与实际 host role/uid/domain不一致 → `EXECUTION_DOMAIN_MISMATCH`；readiness 拒绝 → `NOT_READY`。
7. PFD/type/size/digest/object identity/staging policy 失败 → `ARTIFACT_INVALID`。
8. workspace/render/publish/result I/O 失败 → `IO_FAILURE`；未分类 invariant → `INTERNAL_CONTRACT_FAILURE`。

### 5.2 状态机

```text
NEW --prepare/OK--> PREPARED --start/OK--> RUNNING --claimTerminal/CAS--> TERMINAL
 |                     |                    |                               |
 cancel                cancel               cancel request                  requestClose
 v                     v                    v                               v
TERMINAL(CANCELLED)   TERMINAL(CANCELLED)   AbortPolicy                 DESTROYED

NEW/PREPARED --requestClose/OK--> DESTROYED
RUNNING --requestClose--> UNSAFE_TO_DESTROY（状态不变）
```

| 调用/当前态 | NEW | PREPARED | RUNNING | TERMINAL | DESTROYED |
|---|---|---|---|---|---|
| `setCallback(same identity)` | OK，首次或替换 | OK，替换且不重放 | OK，替换且不重放 | ALREADY_APPLIED，拒绝注册并只读 snapshot | BAD_STATE |
| `prepare(same identity+same normalized content)` | OK→PREPARED | ALREADY_APPLIED | BAD_STATE | BAD_STATE | BAD_STATE |
| `start` | BAD_STATE | OK→RUNNING | ALREADY_APPLIED | BAD_STATE | BAD_STATE |
| `getSnapshot` | OK + snapshot | OK + snapshot | OK + snapshot | OK + immutable snapshot | BAD_STATE + null |
| `cancel` | OK→TERMINAL(CANCELLED) | OK→TERMINAL(CANCELLED) | `AbortPolicy` 决定 OK 或 UNSAFE_TO_CANCEL；后者不改状态 | ALREADY_APPLIED | BAD_STATE |
| `requestClose` | OK→DESTROYED | OK→DESTROYED并清未发布 workspace | UNSAFE_TO_DESTROY且状态不变 | OK→DESTROYED | ALREADY_APPLIED |
| framework `destroy()=16777114` | 未授权 no-op；不清理/不退出 | 未授权 no-op；不清理/不退出 | 未授权 no-op；不得 kill/cleanup | 未授权 no-op；等待 requestClose | idempotent release service resources/exit；不改 terminal snapshot |

RUNNING 的安全取消分成两个不可倒置的线性点：cancel 先在 state-machine mutex 内赢得 cancellation reservation 并返回 OK，但对外 snapshot 仍保持 RUNNING；Flow 停止访问并完成 cleanup 后，worker 才发布唯一 TERMINAL(CANCELLED)。reservation 与 worker publication 之间的 requestClose 固定返回 UNSAFE_TO_DESTROY，因此 workspace、waiter、process、mapping 与 staged artifact 不会在最后访问者停止前被回收。若 worker terminal 先赢，后到 cancel 返回 ALREADY_APPLIED，不能把已完成结果改写成取消。

其它 identity/content 的精确 code 只由 §5.1 决定，不在状态表写“或”。实现以单一 host mutex/serial dispatcher保护状态与 identity；Flow 只通过中立的 `FlowTerminalClaim` 调用一个 `claimTerminal` compare-and-set，并在同一线性点取得 cancellation reservation 后组合 `RunReport`。并发 start/cancel、cancel/terminal、requestClose/terminal 由该 linearization point决定唯一结果；晚到者不产生第二 terminal。RunHost service 调 Flow；Flow 不依赖或反向调用 `AppRunHostService` / `GhostlockUserService`。

callback Binder death：清 callback 引用；NEW/PREPARED 不自动 start，RUNNING 不自动 cancel/kill，worker继续到 terminal并持久化 snapshot；client侧`RunHostLogConnector`drain已接收prefix后返回`Incomplete`，UI标 UNKNOWN且不得重试相同 run。host Binder death：client发布 synthetic `HOST_DIED/UNKNOWN`，终结connector为`Incomplete`并关闭自己持有的 PFD/callback；不自动重启。terminal publication exactly once 与 callback delivery zero-or-one 是两个独立判据。

## 6. 稳定数值合同

### 6.1 `RunHostCode` 与 `RunHostState`

| code | `RunHostCode` | 语义 |
|---:|---|---|
| 0 | `OK` | 状态转移接受 |
| 1 | `ALREADY_APPLIED` | 同 identity 的幂等重复；无新副作用 |
| 10 | `BAD_STATE` | 当前状态不允许调用 |
| 11 | `STALE_GENERATION` | generation 小于 current identity |
| 12 | `RUN_MISMATCH` | 同 generation 的 runId 或 successful-prepare 语义指纹不同 |
| 13 | `BUSY` | generation 大于 current；one-shot host 已被占用 |
| 14 | `INVALID_REQUEST` | grammar/null/type/reference/enum 非法 |
| 15 | `LIMIT_EXCEEDED` | §2 任一预算超限 |
| 16 | `UNKNOWN_BACKEND` | backendKey 未命中 immutable registry |
| 17 | `NOT_READY` | readiness probe 拒绝 |
| 18 | `ARTIFACT_INVALID` | PFD/type/size/digest/identity/staging 校验失败 |
| 19 | `IO_FAILURE` | workspace/staging/render/publish/result I/O 失败 |
| 20 | `UNSAFE_TO_CANCEL` | 当前物理事务不可中止，未 kill |
| 21 | `UNSAFE_TO_DESTROY` | RUNNING，host/process 不得销毁 |
| 22 | `HOST_DIED` | client-side synthetic code；从不由 host 返回 |
| 23 | `INTERNAL_CONTRACT_FAILURE` | invariant/snapshot budget/unknown internal value |
| 24 | `EXECUTION_DOMAIN_MISMATCH` | BackendDescriptor 期望域与实际 host role/uid/domain 不一致 |

unknown code 一律映射 `INTERNAL_CONTRACT_FAILURE/UNKNOWN`，不得当作 OK。AIDL exception/transport error不复用业务 code。

| value | `RunHostState` |
|---:|---|
| 0 | `NEW` |
| 1 | `PREPARED` |
| 2 | `RUNNING` |
| 3 | `TERMINAL` |
| 4 | `DESTROYED` |

### 6.2 `StageState`

| value | state | 约束 |
|---:|---|---|
| 0 | `NOT_RUN` | 尚未开始 |
| 1 | `RUNNING` | 只可出现在 non-terminal snapshot/event |
| 2 | `SUCCESS` | 对应阶段 acceptance 已满足 |
| 3 | `FAILED` | 已知失败 |
| 4 | `CANCELLED` | 仅安全取消已确认 |
| 5 | `UNKNOWN` | 缺证据、host death、malformed 或未知值映射 |
| 6 | `SKIPPED` | 前置未发生且无资源需要 cleanup |

### 6.3 `ItemState`

| value | state |
|---:|---|
| 0 | `NOT_RUN` |
| 1 | `DISPATCHING` |
| 2 | `SUCCESS` |
| 3 | `FAILED` |
| 4 | `SKIPPED_DEPENDENCY` |
| 5 | `UNKNOWN` |
| 6 | `CANCELLED` |

### 6.4 `ExitKind`

| value | kind | `exitValue` range |
|---:|---|---|
| 0 | `NONE` | 必须 0 |
| 1 | `PROCESS_EXIT` | `0..255` |
| 2 | `SIGNAL` | `1..127` |
| 3 | `NATIVE_STATUS` | signed 32-bit 全域；只允许 StageResult，不允许 Payload/Finish ItemResult |

不使用 `-1` 兼任“无退出码”；kind/value 组合非法即 `INTERNAL_CONTRACT_FAILURE/UNKNOWN`。

### 6.5 `ResultReason`

| value | reason |
|---:|---|
| 0 | `NONE` |
| 1 | `NOT_STARTED` |
| 2 | `NOT_READY` |
| 3 | `REJECTED` |
| 4 | `IO_FAILURE` |
| 5 | `PROCESS_EXIT_NONZERO` |
| 6 | `SIGNALLED` |
| 7 | `TIMEOUT` |
| 8 | `CANCELLED` |
| 9 | `HOST_DIED` |
| 10 | `MALFORMED_RESULT` |
| 11 | `ARTIFACT_IDENTITY_MISMATCH` |
| 12 | `RESERVED_DO_NOT_ENCODE` |
| 13 | `INTERNAL_CONTRACT_FAILURE` |
| 14 | `ACK_MISSING` |
| 15 | `CLEANUP_INCOMPLETE` |

value 12 与 unknown reason 都在 decode/construct 边界改成 13，所属 state 改成 UNKNOWN；取消拒绝只由同步 `RunHostCode=20` 表达，不持久化为 stage/item reason。

### 6.6 `RunCapability`、`DiagnosticOutcome` 与 `RunTerminal`

`RunCapability` 是 `BackendDescriptor` 的互斥稳定值，不是 caller request 参数：

| value | capability |
|---:|---|
| 0 | `UNRESOLVED`；尚未 successful prepare，或该阶段产生的 early terminal/close/client synthetic result |
| 1 | `PRODUCT_ROOT` |
| 2 | `DIAGNOSTIC_ONLY` |

`DiagnosticOutcome` 只表达 diagnostic fixture 自身是否完成；它不参与产品成功合成：

| value | outcome |
|---:|---|
| 0 | `NOT_APPLICABLE` |
| 1 | `NOT_RUN` |
| 2 | `PASS` |
| 3 | `FAIL` |
| 4 | `UNKNOWN` |

successful prepare 原子把 `UNRESOLVED` 改为命中 `BackendDescriptor` 的 `PRODUCT_ROOT` 或 `DIAGNOSTIC_ONLY`。从该 resolve point 起，`PRODUCT_ROOT` 的 `diagnosticOutcome` 必须为 `NOT_APPLICABLE`；`DIAGNOSTIC_ONLY` 初始为 `NOT_RUN`，fixture 完整执行并得到确定判断时只能为 `PASS` 或 `FAIL`，host death、malformed fact 或执行证据不完整时为 `UNKNOWN`。安全取消发生在 fixture 开始前时保留 `NOT_RUN`，开始后取消则为 `UNKNOWN`。

| value | terminal |
|---:|---|
| 0 | `NONE` |
| 1 | `SUCCESS` |
| 2 | `DEGRADED` |
| 3 | `FAILED` |
| 4 | `CANCELLED` |
| 5 | `UNKNOWN` |
| 6 | `NEEDS_ATTENTION` |
| 7 | `REBOOT_REQUIRED` |
| 8 | `REBOOT_REQUESTED` |
| 9 | `DIAGNOSTIC_COMPLETE` |

`RunHostState × RunCapability × DiagnosticOutcome × RunTerminal` 的合法组合固定为：

| state / source | capability | diagnostic outcome | terminal | 唯一条件 |
|---|---|---|---|---|
| `NEW` host snapshot | `UNRESOLVED` | `NOT_RUN` | `NONE` | 尚未 successful prepare；可已由 setCallback 预留 identity |
| `PREPARED` 或 `RUNNING` | `PRODUCT_ROOT` | `NOT_APPLICABLE` | `NONE` | successful prepare 已 resolve |
| `PREPARED` 或 `RUNNING` | `DIAGNOSTIC_ONLY` | `NOT_RUN` | `NONE` | fixture 尚未发布 terminal outcome |
| `TERMINAL` host | `UNRESOLVED` | `NOT_RUN` | `CANCELLED` | 仅 setCallback 预留 identity 后、prepare 前的 cancel |
| `TERMINAL` host | `PRODUCT_ROOT` | `NOT_APPLICABLE` | `SUCCESS/DEGRADED/FAILED/CANCELLED/UNKNOWN/NEEDS_ATTENTION/REBOOT_REQUIRED/REBOOT_REQUESTED` | §7.3 产品合成或安全取消 |
| `TERMINAL` host | `DIAGNOSTIC_ONLY` | `PASS` 或 `FAIL` | `DIAGNOSTIC_COMPLETE` | fixture 得到确定判断 |
| `TERMINAL` host | `DIAGNOSTIC_ONLY` | `NOT_RUN` | `CANCELLED` | start 前安全取消 |
| `TERMINAL` host | `DIAGNOSTIC_ONLY` | `UNKNOWN` | `UNKNOWN` 或 `CANCELLED` | 证据不完整，或开始后安全取消 |
| `TERMINAL` client synthetic | `UNRESOLVED` | `UNKNOWN` | `UNKNOWN` | successful prepare 前 host death |
| `TERMINAL` client synthetic | `PRODUCT_ROOT` | `NOT_APPLICABLE` | `UNKNOWN` | resolve 后 host death |
| `TERMINAL` client synthetic | `DIAGNOSTIC_ONLY` | `UNKNOWN` | `UNKNOWN` | resolve 后 host death |
| `DESTROYED` direct close | `UNRESOLVED` / `PRODUCT_ROOT` / `DIAGNOSTIC_ONLY` | 分别 `NOT_RUN` / `NOT_APPLICABLE` / `NOT_RUN` | `NONE` | NEW/PREPARED 的 requestClose；不发布 terminal snapshot |
| `DESTROYED` after terminal | 保留 terminal 前 capability | 保留 terminal outcome | 保留 terminal | requestClose 只改变 host lifecycle，不改 immutable terminal facts |

NEW 的 cancel/requestClose 只有在 identity 已由 setCallback 预留时才走上表；identity 尚未建立时仍按 §5.1 返回 `BAD_STATE`。client synthetic `TERMINAL` 是本地 report/snapshot projection，不冒充 host durable terminal publication，也不触发第二 callback。任何其它组合都映射 `lastCode=23`、`diagnosticOutcome=UNKNOWN`（已 resolve 的 product run 仍保持 `NOT_APPLICABLE`）与 `terminal=UNKNOWN`。产品成功 projector 的唯一真值是 `runCapability=PRODUCT_ROOT && terminal=SUCCESS`；`DIAGNOSTIC_ONLY + PASS + DIAGNOSTIC_COMPLETE` 必须投影为 false。

### 6.7 Result 与 event 合法组合矩阵

`StageResultParcel` 只接受下列 conjunction；未列组合在 decode/construct 边界统一变为 `state=UNKNOWN, reason=INTERNAL_CONTRACT_FAILURE, exitKind=NONE, exitValue=0`，同时 snapshot `lastCode=23`：

| StageState | 允许 reason | 允许 exit |
|---|---|---|
| `NOT_RUN` | 仅 `NOT_STARTED` | 仅 `NONE/0` |
| `RUNNING` | 仅 `NONE` | 仅 `NONE/0` |
| `SUCCESS` | 仅 `NONE` | `NONE/0`、`PROCESS_EXIT/0` 或 `NATIVE_STATUS/0` |
| `FAILED` | `NOT_READY`、`REJECTED`、`IO_FAILURE`、`PROCESS_EXIT_NONZERO`、`SIGNALLED`、`TIMEOUT`、`ARTIFACT_IDENTITY_MISMATCH`、`INTERNAL_CONTRACT_FAILURE`、`CLEANUP_INCOMPLETE` | reason=`PROCESS_EXIT_NONZERO` 时恰为 `PROCESS_EXIT/1..255`；reason=`SIGNALLED` 时恰为 `SIGNAL/1..127`；reason=`REJECTED` 仅允许 `NONE/0` 或非零 `NATIVE_STATUS`；其它 reason 仅 `NONE/0` |
| `CANCELLED` | 仅 `CANCELLED` | 仅 `NONE/0` |
| `UNKNOWN` | `HOST_DIED`、`MALFORMED_RESULT`、`INTERNAL_CONTRACT_FAILURE`、`ACK_MISSING` | 仅 `NONE/0` |
| `SKIPPED` | 仅 `NOT_STARTED` | 仅 `NONE/0` |

`ItemResultParcel` 只接受下列 conjunction；非法组合使整个 GLR1/result list 成为 `MALFORMED_RESULT/UNKNOWN`，不能逐项修补：

| ItemState | 允许 reason | 允许 exit |
|---|---|---|
| `NOT_RUN` | 仅 `NOT_STARTED` | 仅 `NONE/0` |
| `DISPATCHING` | 仅 `NONE` | 仅 `NONE/0`；complete file 中仅 §7.4 reboot item 可保留 |
| `SUCCESS` | 仅 `NONE` | `NONE/0` 或 `PROCESS_EXIT/0` |
| `FAILED` | `REJECTED`、`IO_FAILURE`、`PROCESS_EXIT_NONZERO`、`SIGNALLED`、`TIMEOUT` | process/signal reason 与 §6.4 exact 对应；其它 reason 仅 `NONE/0` |
| `SKIPPED_DEPENDENCY` | 仅 `NOT_STARTED` | 仅 `NONE/0` |
| `UNKNOWN` | `HOST_DIED`、`MALFORMED_RESULT`、`INTERNAL_CONTRACT_FAILURE`、`ACK_MISSING` | 仅 `NONE/0` |
| `CANCELLED` | 仅 `CANCELLED` | 仅 `NONE/0` |

`RunHostLogRecord` 不携带 stage/state/reason/exit/report 字段。`origin + level + message` 只描述展示语义；unknown enum、超限、非法 ASCII/SGR、identity 不匹配、重复或倒序 sequence 一律丢弃并增加诊断丢弃计数，绝不改变 terminal。六层 `RunReport` 由 Flow 从 typed execution facts 组合并返回，`LogHub` 只在 terminal commit 中将它投影成普通 canonical Info records；UI 不维护第二套 report renderer。

## 7. `GLR1` root result 与 terminal 合成

### 7.1 唯一 line protocol

本节是 root result file 的唯一 grammar；主计划只链接本节，不再重复半定义格式。文件必须 ASCII、LF-only、末尾换行、无 NUL/CR，且总长不超过 256 KiB。

```text
file          = "GLR1\n" run generation payload-record* finish-record* handoff complete
run           = "run=" run-id "\n"
generation    = "generation=" positive-decimal "\n"
payload-record= "payload." item-id ".state=" item-state "\n"
                "payload." item-id ".reason=" result-reason "\n"
                "payload." item-id ".exit_kind=" item-exit-kind "\n"
                "payload." item-id ".exit_value=" item-exit-value "\n"
finish-record = "finish." item-id ".state=" item-state "\n"
                "finish." item-id ".reason=" result-reason "\n"
                "finish." item-id ".exit_kind=" item-exit-kind "\n"
                "finish." item-id ".exit_value=" item-exit-value "\n"
handoff       = "handoff=" script-handoff "\n"
complete      = "complete=1\n"
item-state    = "0" | "1" | "2" | "3" | "4" | "5" | "6"
result-reason = "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" |
                "8" | "9" | "10" | "11" | "13" | "14" | "15"
item-exit-kind= "0" | "1" | "2"
item-exit-value= "0" | positive-decimal
script-handoff= "0" | "1"
positive-decimal = nonzero-digit digit*
digit         = "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9"
nonzero-digit = "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9"
```

`script-handoff=0` 只表示脚本正常返回到 carrier，`1` 只表示 reboot durable handoff 已写；它不是产品 `RunTerminal`，也不能表达 SUCCESS/FAILED/DEGRADED。`run-id`、`item-id` 与 generation 分别服从 §2 grammar/range；十进制禁止 `+`、空白与非零值的前导零，item exit value 还必须服从 §6.4 的 kind-dependent range。payload/finish record 必须各按 frozen request 顺序恰好出现一次，同一 record 的四行必须连续且 itemId 相同。complete file 中 Payload 不得停在 DISPATCHING 或 NOT_RUN；Finish 的 DISPATCHING 只允许在 `handoff=1` 时恰为 frozen request 最后一项的唯一 REBOOT，普通 `handoff=0` complete file 不允许任何 DISPATCHING/NOT_RUN。unknown key、重复/缺失/换序/跨 phase ID、非法 state/reason/kind/value 组合、非法 handoff、缺 complete 或 complete 后有内容都使整个 parser 返回 `MALFORMED_RESULT/UNKNOWN`；不保留“可用的部分”冒充完成结果。

### 7.2 Payload/Finish overall

本节只对 `PRODUCT_ROOT` 合成产品阶段。`DIAGNOSTIC_ONLY` 的两个 item list 必须为空，`payloadOverall` 与 `finishOverall` 固定为 `SKIPPED/NOT_STARTED`，唯一终态按本节末尾的 diagnostic 分支合成；这两个 SKIPPED 不是产品成功证据。

Payload 规则是确定性的：

1. frozen Payload 必须有 `1..64` 项且至少一个 required。按顺序执行；第一个 required `FAILED`、`UNKNOWN` 或 `CANCELLED` 是 fail-fast trigger，后续所有 Payload 必须恰为 `SKIPPED_DEPENDENCY/NOT_STARTED`。trigger 前出现 skip、trigger 后仍执行 item、complete result 出现 NOT_RUN/DISPATCHING 都使整个结果 malformed。
2. 顺序 fail-fast 只允许一个 required 非成功 trigger：无 trigger 时 `payloadOverall=SUCCESS`；有 trigger 时 `payloadOverall` 直接继承该第一项的 FAILED/UNKNOWN/CANCELLED。trigger 后若第二个 required 又出现 FAILED/UNKNOWN/CANCELLED，不做 mixed precedence，整个结果固定 malformed；合法 dependency skip 只继承 trigger，不制造第二个失败等级。
3. optional item 不改变 `payloadOverall`：optional UNKNOWN 直接使最终 terminal UNKNOWN；optional FAILED 或 CANCELLED 使最终 terminal DEGRADED；因更早 required trigger 而合法 skip 的 optional item不再降级。所有 required 成功时出现 optional skip 是 malformed。

Finish 是 ordered best-effort，不因某个 Finish 自身失败而跳过后续项；`alwaysRun` 只决定 Payload 未成功时是否仍须尝试：

1. product Finish 为空时，`finishItems=[]` 且 `finishOverall=SUCCESS/NONE/NONE/0`，这是 neutral empty-set 结果，不凭空生成 item。
2. Payload 成功时，每个非 REBOOT Finish 都必须得到 SUCCESS/FAILED/UNKNOWN/CANCELLED；任一 `SKIPPED_DEPENDENCY` 或 NOT_RUN 都是 malformed。Payload 非成功时，`alwaysRun=false` 必须为 `SKIPPED_DEPENDENCY/NOT_STARTED`，`alwaysRun=true` 必须实际尝试并得到终态；把 alwaysRun item skip/NOT_RUN 是 malformed。
3. 普通 `handoff=0` complete result 的 `finishOverall` 优先级固定为 `UNKNOWN > FAILED > CANCELLED > SKIPPED > SUCCESS`。SKIPPED 只来自上条允许的 non-alwaysRun dependency skip；complete normal result 不允许 NOT_RUN。
4. `handoff=1` 时，唯一且最后的 REBOOT item 可保持 `DISPATCHING/NONE`；durable handoff 是该 item 的 acceptance，因此聚合时把它视为 SUCCESS，但不宣称设备已经 reboot。其它 Finish 仍按上条聚合；§7.3 以 `REBOOT_REQUESTED` 覆盖普通产品 terminal。

renderer/freezer 持有 `required/severity/alwaysRun` authority；result file 只按 itemId 关联，不能自行改这些策略。顺序 Payload 出现 required UNKNOWN+FAILED 或 FAILED+CANCELLED 固定 malformed；Finish 不 fail-fast，mixed set 才严格使用其优先级，例如 Finish FAILED+CANCELLED→FAILED、Finish CANCELLED+SKIPPED→CANCELLED。

### 7.3 六层 facts → terminal 唯一优先级

先按 `RunCapability` 分支，禁止把 diagnostic facts 送入产品表：

- `DIAGNOSTIC_ONLY`：Payload/Finish 必须为空；确定 `DiagnosticOutcome.PASS` 或 `.FAIL` 都得到 `DIAGNOSTIC_COMPLETE`，产品成功 projector 恒为 false。证据不完整/host death/malformed 得到 `DiagnosticOutcome.UNKNOWN + RunTerminal.UNKNOWN`；安全取消按 §6.6 得到 CANCELLED。
- `PRODUCT_ROOT`：`diagnosticOutcome` 必须 `NOT_APPLICABLE`，RunHost 从下表由上到下取第一个命中项；日志、stdout、event 不参与。

| 优先级 | 条件 | terminal |
|---:|---|---|
| 1 | 有 identity 匹配且 `complete=1` 的 durable script result 写 `handoff=1` | `REBOOT_REQUESTED` |
| 2 | backend terminal hint 明确 held/parked state 要求 reboot | `REBOOT_REQUIRED` |
| 3 | backend cleanup FAILED/CLEANUP_INCOMPLETE，或任一 SAFETY_CRITICAL Finish FAILED | `NEEDS_ATTENTION` |
| 4 | 任一必需阶段/item UNKNOWN、malformed result、host death、optional item UNKNOWN | `UNKNOWN` |
| 5 | 安全取消已确认，且没有 1–4 的状态 | `CANCELLED` |
| 6 | primitive、dispatch 或 payloadOverall FAILED；或必需 Payload 被 dependency skip | `FAILED` |
| 7 | 全部 required Payload 成功，但 optional Payload FAILED/CANCELLED 或 NORMAL Finish FAILED | `DEGRADED` |
| 8 | primitive=SUCCESS、dispatch=SUCCESS、payloadOverall=SUCCESS、finishOverall=SUCCESS、cleanup=SUCCESS | `SUCCESS` |
| 9 | 其它未覆盖组合 | `UNKNOWN` |

raw uid 0 只能影响 primitive，不会跳过 dispatch/Payload/cleanup 条件。若 primitive 从未开始，cleanup=`SKIPPED/NOT_STARTED` 可随已知 primitive failure 合成为 FAILED；只要 primitive 已 RUNNING，cleanup 未 SUCCESS 就不能得到 SUCCESS。

### 7.4 reboot durable handoff

Finish 请求 reboot 时，该 REBOOT 必须是 frozen Finish 的唯一且最后一个 REBOOT。脚本必须在 reboot syscall 前写完整 fixed-order file，其中对应项为 `state=1`、`reason=0`、`exit_kind=0`、`exit_value=0`，并写 `handoff=1`、`complete=1`，随后 flush + `fsync` 预创建的 result fd。唯一写法是带 `.state` 后缀的 `finish.<id>.state=1`，不另设第二语法；不存在“REBOOT 后还有待执行 Finish”的合法 request。

client 收到 callback 前设备重启时，下次启动只可从 identity 匹配且 durable complete 的 result 恢复 `REBOOT_REQUESTED` 与已写 facts；不得补写 SUCCESS。pre-handoff reboot、host death或 partial file 都保持 UNKNOWN。held zombie/park 由 backend hint 合成 REBOOT_REQUIRED。

## 8. P-RUN-X-001/批次01 验证矩阵

1. 所有 AIDL 方法 transaction id exact 对拍（含 `destroy=16777114`）与方法×状态逐格测试；特别覆盖 setCallback→cancel=`UNRESOLVED/NOT_RUN/CANCELLED`、setCallback→requestClose=`DESTROYED/UNRESOLVED/NOT_RUN/NONE`、successful prepare 前 host death=`UNRESOLVED/UNKNOWN/UNKNOWN`；重复/并发调用验证唯一 linearization、terminal publication exactly once、callback delivery 0/1。
2. identity/error priority：runId/generation 的 empty、0、-1、max、非法字符、lower/same-different/higher；同一输入同时命中两种错误时逐项证明 §5.1 优先级。
3. budgets/shape：每个 count/bytes 的 0、1、limit、limit+1；PRODUCT_ROOT 的 Payload 0/1/全 optional/至少一 required 与 Finish 0/1/32/33；DIAGNOSTIC_ONLY 的两个 empty list 正例及任一 non-empty 负例；request/snapshot/event 实际 Parcel 大小而非估算。
4. request enums：Payload kind×acceptance×artifact×args 与 Finish kind×artifact×args×alwaysRun 全表正例；REBOOT 非最后/重复；每个 unknown/reserved 值和每个跨行组合必须 `INVALID_REQUEST`。
5. artifacts：非 regular、pipe/socket/directory、size mismatch、digest mismatch、duplicate logicalId、fd提前关闭、read error，以及 publish 后 rename/truncate/symlink/path replacement、inode/device/mode mismatch、carrier namespace 指向另一对象；验证必须基于同一 opened fd，不能只重查 path。
6. callbacks：event enum/field conjunction、sequence 16,384条及Long耗尽、8 KiB/8 KiB+1、12 KiB Parcel、Binder death、late callback、no replay、unknown enum只降级不升级。
7. lifecycle：App unbind、Shizuku callback death、对 transaction 16777114 的 premature direct call、RUNNING unsafe close、requestClose 后 destroy、host crash；提前 destroy 必须观测为 no-op且 worker/resource未被清理。
8. result protocol：§6.7 每个合法/非法 state×reason×exit conjunction、每个 stable integer（含 ResultReason 12 必须拒绝并映射 13/UNKNOWN）、顺序/重复/缺失/unknown key、256 KiB边界、partial/complete、handoff=0/1、reboot pre/post-fsync；Payload required `UNKNOWN+FAILED`、`FAILED+CANCELLED` 必须 malformed，另测单一 trigger、all-success、optional failed/unknown、合法/非法 skip；Finish empty、`FAILED+CANCELLED`、`CANCELLED+SKIPPED`、alwaysRun skip 与普通 NOT_RUN；六层 terminal 合成表逐行与组合穷举。
9. capability/cross-process：同一 request 在 App RunHost 与 Shizuku RunHost 得到同一 validation/result code；BackendDescriptor domain 与实际 pid/uid/SELinux不一致固定为 code 24。PRODUCT_ROOT+SUCCESS 是产品 success 唯一正例；`DIAGNOSTIC_ONLY+PASS+DIAGNOSTIC_COMPLETE`、diagnostic FAIL/UNKNOWN、capability/outcome/terminal 非法组合都必须证明不能升级产品 success。
