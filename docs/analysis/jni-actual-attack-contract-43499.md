# 43499 JNI 字段定义

> 主契约见 `jni-actual-attack-contract.md`。本文件解释 43499 字段语义；可执行字段顺序与 JVM
> 类型的唯一权威是 `core/src/jni/attack_contract_fields.def`，生成物不得手改。

## 1. Init43499

会话级 route 配置：

- identity：`routeId`；CPU：`mainCpu/consumerCpu`，`-1` 明确表示不绑定。
- stamp：`waiterOffset/taskOffset/lockOffset/bufferSize`。
- limits：`maxAttempts/armSequence/armHold`。
- timing：`parkMs/settleUs/postRequeueSettleUs/postAdjustSettleUs/consumerDelayUs/
  fallbackLockUs`。
- consumer：`burstCalls/maxCalls/niceModulus/policy`。
- route 专属闭集：multicast=`armRounds/stampWaiterShift`；TCP=`zeroCopyMapBytes`；
  select=`waiterShift/fdCount`。不适用于所选 route 的组必须 absent，不能静默忽略。

`KernelLayout43499`：

- payload slots：`lock/waiter/fakeTask/credCopy/fopsTable`。
- lock fields：`owner/waitLock/waitList/listLink`。
- waiter fields：`headWord/headNext/headPrev/pc/right/left/task/lock/wakeState/prio/deadline/wwCtx`。
- task fields：`usage/prio/normalPrio/piLock/piWaiters/piTopTask/piBlockedOn/group`。
- payload values：`taskUsage/taskPrio/waiterPrio`。
- image offsets：`initTask/rootTaskGroup/initCred/securityHookHeads`；
  `pointerBytes`。

`CredentialLayout43499`：`copySize/usageOffset/usageValue/capsOffset/capsCount/capsValue/
refOffsets[4]/refImages[4]/refCount`。

`TargetOffsets43499` 与 fake-task layout 分离且唯一持有：`selinuxEnforcingImage/taskCred/taskRealCred/
taskAtomicFlags/taskSeccomp`，每项明确拆成 `Offset/Value/Present/Width` 四字段；`Offset` 是相对
image 或 task_struct 的位置，`Value` 是目标写入值，两者不得混用。image offset 宽 8，task_struct
offset 宽 4。它们只从已解析 profile 快照的一处读取；Kotlin 用运行期 base/task 加 offset 计算地址，
native 再做范围校验。缺哪个 present 就具名拒绝，不以 0 猜测。

## 2. Prepare43499

- snitch：`collisions/threads/futexTableSize/roundWaitUs/wakeRounds/possibleCpus/mmSlabOrder`。
- budgets：`prepareMaxAttempts/prepareTimeoutMs/kernelSnitchTimeoutMs/routeWaitMs/
  routeDoneTimeoutMs/setupSettleUs/statePollIntervalUs`。
- region：`headerBytes/totalPokes/pageSize/regionSize/objectSize/mmStructSize/regionThreads/
  mainCpu/pollRounds/preGroups/preShapedGroups/sprayGroups/postGroups/leakGroups`。
- strides/shaping：`wordBytes/handleBytes/coarseStride/slabStride/mmObjectsPerSlab/shaped/
  socketBuffer/sendSize`。
- proof：`proofSeed/proofCount/counterOffset`。

PrepareResult：`pageBase/word0/word1/payloadPage/payloadBase/fakeTask/fakeLock/proofs[]`；
`opened/closed/openedPre/openedSpray/openedPost/openedLeak/closedPre/closedSpray/closedPost`；
`status/atStep/error/snitchVerdict/snitchLoopSaw/snitchLoopAddress/snitchProbes/snitchRounds/
snitchFound/shapingReady/shapingOwned/shapingTransferred`。

资源所有权：region、words、payload page、shaping fd 在会话内持有；结果只发布标量快照。任何失败路径
按创建逆序释放；已交给 route 的 shaping fd 不由 prepare 再释放。

## 3. Discover43499

独立事务，字段对应 `DiscoveryRequest`：`setup` 与 prepare 相同；`region/patience/strides` 同源；
`rangeStart/rangeEnd/counterOffset/sweepTags`。DiscoverResult：
`verdict/found/mmStruct/probes/rounds/pokesWanted/error`。

prepare 只给 discover 留持久 region/words，不运行第二次 ObjectWalk。discover 成功后发布 generation；
重新 prepare 会使旧 discovery generation 和旧 write target 失效。

## 4. 写与 victim

`WordWrite43499(address,value,form,offset,offsetPresent,prepareGeneration,discoveryGeneration,
victimGeneration,victimGenerationPresent)`；form 闭集
为 `CLEAR_PRESERVING/CLEAR_LEAF/VALUE_PRESERVING`。native 用 KernelLayout、CredentialLayout 与本次
请求原子重建 payload tuple；全量边界检查通过前不改 payload page。

W1 要求 victimGenerationPresent=false；W2/W3 要求 true 且代际属于当前 Owned/Parked victim。
重新 spawn、HANDOFF 或 RETIRE 都使旧代际失效。

WriteResult：通用 receipt；`statusCode/statusStep/errorNumber/userspaceClean/kernelDisarmed`；
`attempts/calls/successes/stampResult/stampErrno/requeueResult/waiterResult/ownerResult/parkErrno`。

`spawnVictim` 返回 `victimGeneration/pid/task`。`victimProbe` 必须带 generation。
`finishVictim(PARK)` 使成功对象永久驻留；`HANDOFF` 释放 native 所有权并返回 pid；`RETIRE` 只用于
失败对象。旧 owner 在 HANDOFF 后不得 signal/wait/close 该对象。
