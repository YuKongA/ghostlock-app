# 43284 JNI 字段定义

> 主契约见 `jni-actual-attack-contract.md`。本文件解释 43284 字段语义；可执行字段顺序与 JVM
> 类型的唯一权威是 `core/src/jni/attack_contract_fields.def`，生成物不得手改。

## 1. Init43284

`spi:u32/encapPort:u16/senderPort:u16/icvLength:u8/aesKey:bytes[32]/hmacKey:bytes[32]/
initialSequence:u32/sequencePresent:bool`。

密钥是天然字节，允许 ByteArray；JNI 立即复制进会话定长缓冲。所有 Kotlin/native 临时副本在
成功和失败路径清零；D1 native 投影使用 move 后源对象也擦除的 `ZeroizingBytes32`。initialize 建立
session-wide SA/peer/socket，不建立依赖路径的 door/bridge。生成的 JNI wrapper 在返回前还会清零
DTO 内两个 32-byte 数组，失败于任一后续字段也走同一清理出口。

## 2. target 与 block

TargetRole 闭集：`PATCH1/CARRIER/HOOK`；AccessMode 闭集：`DIRECT/REMOTE_BRIDGE`。
`resolveTarget(role,path,accessMode)` 才建立对应 door，分配 target slot，返回不透明
`ticket(index,generation)` 与 `canRead`。slot 内保存完整 native token、role、session generation、
target generation、路径身份；三 role 可同时存活。

所有变长字节在 JNI 分配前执行单表 `allocationMaxBytes`：path/vermagic/marker ≤4096 B，
hookSymbol ≤512 B，moduleName ≤256 B，moduleImage/hookImage ≤64 MiB；它只防止跨端分配失控，
不替代 D2 backend 的既有语义上限（例如 door path 与 vermagic parser）；两层分别具名拒绝。

REMOTE_BRIDGE 的 CARRIER/HOOK 读取已 Verified 的 PATCH1 helper path；缺失时拒绝
`not-ready:patch1-verified`。`verifyTarget(ticket,expectedDigest,size)` 由 native 经同一 read surface
读取并以公共 Digest32 比对，成功后将 slot 标成 Verified；PATCH1 未 Verified 不能绑定 remote
bridge。resolve remote 时把 helper/carrier path、PATCH1 generation 和 digest 复制进新 slot；随后
PATCH1 re-resolve 不使已构造的 remote slot 悬空或失效。

`writeBlock(ticket,offset,block16)` 与 `readBlock(ticket,offset)` 的 offset 为文件偏移，零合法。
WriteResult 返回 `offset/width=16/sequenceBefore/sequenceAfter/effect/targetGeneration`。send 前失败
不推进 sequence；send 成功后推进、保存 expected block 并返回 DISPATCHED_UNVERIFIED。VerifyPending
只允许同 ticket/offset 的 readBlock：native 比较后，匹配为 `OK+APPLIED`，失配为
`FAILED:verify-mismatch+NOT_APPLIED`，都清 pending；读取失败保留 pending 以便重读。是否发送不可判定
才 INDETERMINATE/Frozen。普通 read 固定 16 字节；短输出在设备动作前拒绝。

## 3. ModuleCheck43284

输入：`moduleImage` 与完整 `kernelVermagic`。禁止仅传 release+默认 feature token；kernelVermagic
由 profile/设备事实的唯一权威提供。输出：`elfValid/hasModinfo/name/vermagic/hasCrcs/
signedModule/cfiPresent/vermagicMatchesKernel/verdict`。该调用纯分析，不改变 SA/target/sequence。

## 4. HookPlanRequest

输入：`hookImage`、`hookSymbol`、`guardPolicy`。guardPolicy 是闭集 `REQUIRE/SKIP_IF_MATCHED`；不得
使用编译期 device_sentry_plan 作为跨镜像答案。

输出：`imageDigest:Digest32/guardInstruction/guardSkipped/hookFileOffset/hookVaddr/
displacedInstruction/payloadFileOffset/payloadVaddr/payloadMaxBytes/entryOffset/
displacedOffset/jumpBackOffset/branchWord/payload`。payload 与计划一次生成并绑定 digest；Kotlin 在
写 hook 前核对同一 digest。`embeddedImage` 的角色闭集只留 `SPLICE_HELPER`。

## 5. Wait43284

`Wait43284(budgetMs,pollMs,successMarker,failureMarker,inFlightMarker,moduleMarker,moduleName)`。
所有字符串带显式长度且不能为空半对。

一次轮询的判定顺序：failure 命中→FAILED；success 命中→OK；此前见过 module 而本轮消失→FAILED；
inFlight 命中→IN_FLIGHT；module 当前存在→继续轮询；预算耗尽→IN_FLIGHT。WaitResult 返回
`terminal/sequence/polls/elapsedMs/moduleSeen`。trigger 只接收 `delayMs`，返回
`forked/detached/intermediateReaped/execStatusKnown`；不把中间 child pid 冒充最终执行者。

## 6. 生命周期

会话拥有 SA、sequence、socket、door/bridge 与三个 target slot。重新 resolve 只回收同 role 的旧
slot。shutdown 停止新 block，等待在飞 block，关闭 target/bridge/socket，最后清零 SA；任一写效果
不确定时会话先 Frozen，再允许 shutdown。
