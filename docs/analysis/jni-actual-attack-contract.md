# 两个 backend 的执行契约审查与定义

> 状态：待批准；补充 `jni-two-backend-plan.md` §10。基线：`e9d2ea0d`，2026-10-14。
> 字段权威见 [43499](jni-actual-attack-contract-43499.md) 与
> [43284](jni-actual-attack-contract-43284.md)。
> design-critic：2026-10-14，最终总判“可进批准 gate”，阻断 0。

## 1. 总判与边界

每个 backend 一个 JNI 库、每个不可分割 native 事务一个具名函数，方向成立。旧定义只表达了
步骤名，尚未承载实际设备实现所需参数；本稿冻结函数、状态和结果，两份附件冻结字段。

- Kotlin：步骤顺序、目标地址、重试、等待策略和文件读取。
- native：跨调用资源、秘密、sequence、内核地址事实、PI 窗口、door/bridge 和终态判据。
- 有副作用的调用原子返回 `code + reason + receipt`；不得二次查询 `refusal(handle)`。
- JNI 不传 profile、C++ 指针、裸 `mode/leaf`、op/key 号或通用字节帧。
- 参数类、JNI 读取器和字段清单由一张模型生成；禁止字段名表与索引映射各写一份。

## 2. 共用定义

`NativeCode` 的整数冻结为：`OK=0`、`NOT_READY=-1`、`BAD_PARAMETERS=-2`、
`UNSUPPORTED=-3`、`FAILED=-4`、`STALE_HANDLE=-5`、`BUSY=-6`、`STALE_TARGET=-7`、
`IN_FLIGHT=-8`、`CLOSED=-9`、`INDETERMINATE=-10`。

Kotlin 统一结果为 `NativeAnswer<T>(code,reason,value)`。写回执为
`WriteReceipt(offset,width,sequenceBefore,sequenceAfter,effect)`；effect 是
`APPLIED/NOT_APPLIED/DISPATCHED_UNVERIFIED/INDETERMINATE`。create 返回 `handle + answer`；destroy 定义为
`destroy(handle,out):Int`，至少区分 `OK/STALE_HANDLE/BUSY`。

`Digest32` 唯一定义为 `SHA-256(rawBytes[0,size))` 的 32 个原始字节，不接受十六进制字符串。
`size` 必须大于 0 且不超过该 target/image 的可读长度；比较必须访问全部 32 字节。HookPlan、
verifyTarget 和 Kotlin 对拍均由这一生成类型提供，禁止第二套摘要算法或编码。

## 3. 状态机

| 状态 | 允许操作 | 失败后的去向 |
|---|---|---|
| `Created` | initialize, snapshot, shutdown, destroy | 参数错留原态；设备接线失败到 Failed |
| `Ready` | backend 的准备/解析操作、snapshot、shutdown, destroy | 可确定未生效留原态 |
| `Prepared/Resolved` | 数据操作、查询、snapshot、shutdown, destroy | 同上 |
| `VerifyPending` | 对应 ticket/offset 的 readBlock、snapshot、shutdown, destroy | 匹配或失配回原态；读失败留本态 |
| `Frozen` | snapshot, shutdown, destroy | 任何不确定副作用进入此态 |
| `Failed` | snapshot, shutdown, destroy | 不再重试设备动作 |
| `Closed` | snapshot, shutdown, destroy | shutdown 幂等；其它一律 CLOSED |

同 handle 的调用采用 try-lock：已在飞返回 BUSY。shutdown 先禁止新调用，再等在飞调用结束并按
资源依赖逆序回收；destroy 必须执行 shutdown。重复 initialize 一律 `BAD_PARAMETERS:already-initialized`。

## 4. 43499 函数闭集

`create`、`initialize(Init43499)`、`prepare(Prepare43499)`、`discover(Discover43499)`、
`spawnVictim`、`victimProbe(kind)`、`finishVictim(action)`、`writeWord(WordWrite43499)`、
`readWord(address,offset,offsetPresent)`、`snapshot`、`shutdown`、`destroy`。

本设计选择独立 discover：prepare 只建立持久资源、words、payload 映射与证明地址；discover
接收完整 DiscoveryRequest，发布 found/mmStruct。两条实现不得重复搜索。

`writeWord` 是一笔事务：用本次 address/value/form 对会话持有的 payload 映射做 all-or-nothing
重建，再 arm → 三角色窗口 → disarm → 回执；因此 W1/W2/W3 可在同会话连续改变 tuple。
开窗后参数被冻结；generation 变化才返回 STALE_TARGET。W1 只依赖 prepare+discover；W2/W3
另依赖 victim。`offsetPresent` 保留，因为零是合法偏移。
W2/W3 请求必须带当前 `victimGeneration`；W1 必须明确 absent。重新 spawn 或 finish 会使旧 victim
generation 的写请求返回 STALE_TARGET。

victim 状态为 `Owned → Parked/HandedOff/Retired`。`finishVictim(PARK/HANDOFF/RETIRE)` 返回 receipt；
shutdown 只回收仍 Owned 的失败 victim，不杀 Parked/HandedOff 对象。

WriteResult 除通用 receipt 外返回 route 的 `statusCode/statusStep/errorNumber/userspaceClean/
kernelDisarmed` 及 attempts/calls/successes/stamp/requeue/waiter/owner/park errno。字段来源见附件。

## 5. 43284 函数闭集

`create`、`initialize(Init43284)`、`resolveTarget(role,path,accessMode)`、
`writeBlock(ticket,offset,block16)`、`readBlock(ticket,offset)`、`verifyTarget(ticket,digest,size)`、
`analyzeModule(ModuleCheck43284)`、
`deriveHookPlan(HookPlanRequest)`、`embeddedImage(SPLICE_HELPER)`、`trigger(delayMs)`、
`waitTerminal(Wait43284)`、`snapshot`、`shutdown`、`destroy`。

ticket 是 native target registry 的不透明 `index+generation`，不是 native token 的 bit-pack。
slot 内保存完整 token、role、session generation 和 target generation；调用同时校验 session handle。
patch1/carrier/hook 各有独立 slot，重新解析一个 role 不使另两个失效。

sequence 只由 native SA 会话推进。send 前失败为 NOT_APPLIED 且 sequence 不变；send 成功即推进
sequence，将 expected block 存入 pending，并返回 DISPATCHED_UNVERIFIED/VerifyPending。此时对应
ticket/offset 的 readBlock 由 native 比较 pending：匹配返回 `OK+APPLIED+bytes`，失配返回
`FAILED:verify-mismatch+NOT_APPLIED+bytes`，两者都清 pending 回 Resolved；读取失败留 VerifyPending，
允许重读但不重发。其它 read 拒绝 NOT_READY:write-verification。syscall 是否消耗 sequence 不可判定
才返回 INDETERMINATE/Frozen。普通 readBlock 区分 UNSUPPORTED 与 FAILED；canRead 属于 target。

hook payload 只由 digest-bound `deriveHookPlan` 返回；`embeddedImage` 不再返回固定 sentry payload。
initialize 只持有 SA/peer/socket；door 在 resolveTarget(path) 建立。accessMode 闭集为 DIRECT/
REMOTE_BRIDGE；CARRIER/HOOK 的 REMOTE_BRIDGE 要求 PATCH1 slot 已由 verifyTarget 标成 Verified，
否则 NOT_READY:patch1-verified。remote slot 在 resolve 时复制 helper/carrier path、PATCH1 generation 和
Digest32；之后 PATCH1 re-resolve 不悬空，也不改变已经构造的 remote slot。
waitTerminal 顺序冻结为 failure → success → module-gone → in-flight；in-flight 命中立即返回
IN_FLIGHT，module 当前存在则继续轮询，曾存在后消失则 FAILED。

## 6. 装配与门禁

create 必须经 backend factory 装入真实 seams；空 seam 只允许 host 测试库。43499 会话拥有准备
区域、words、payload 映射、victim、route/window/线程；43284 拥有 SA、door/bridge、三个 target
slot 和 sequence。shutdown 顺序：停止访问 → 等在飞事务 → disarm/停线程或 socket → 关 fd/映射
→ 按 victim 状态终结 → 清零秘密 → Closed。

1. **D1 定义生成（已完成 host/NDK 门禁）**：`core/src/jni/attack_contract_fields.def`
   单表投影 Kotlin 类型/具名 native Values/JNI 读取器/TSV 字段清单；宿主逐字段分型哨兵、长度
   极值与失败不改输出，交换任意相邻字段必须红。真实 JVM→JNI adapter 异常注入留 D2 设备门禁。
2. **D2 设备装配**：先接 43284 SA/door/sequence，再接 43499 prepare/discover/route/payload。
3. **D3 Kotlin 适配**：上层只读 NativeAnswer、facts、receipt，不解释 native token/指针。
4. **D4 真机**：43284 复现既有门禁；43499 分 prepare、一次 window、三步链。成功前不删旧路径。
