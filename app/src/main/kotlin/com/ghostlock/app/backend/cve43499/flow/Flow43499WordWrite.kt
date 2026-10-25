package com.ghostlock.app.backend.cve43499.flow

/**
 * native `WordWrite43499.form` 的三值闭集：1=ClearPreserving、2=ClearLeaf、
 * 3=ValuePreserving；4/越界是非法值，native 返回 `write-refused:form`。
 */
enum class Flow43499WriteForm(val wire: Int) {
    CLEAR_PRESERVING(1),
    CLEAR_LEAF(2),
    VALUE_PRESERVING(3),
}

/**
 * native `WordWrite43499` 的 9 个字段。
 *
 * 这是**写请求**，不是会话级 Init；一次 run 的每个 step 各自持有一条。
 * `target == address` 由 Flow 侧校验（K1 约束），不另存第二份 target。
 */
data class Flow43499WordWrite(
    val address: Long,
    val value: Long,
    val form: Flow43499WriteForm,
    val offset: Long = 0L,
    val offsetPresent: Boolean = false,
    val prepareGeneration: Long = 0L,
    val discoveryGeneration: Long = 0L,
    val victimGeneration: Long = 0L,
    val victimGenerationPresent: Boolean = false,
)

/**
 * native `FinishVictim43499.action`：0=RETIRE、1=PARK。
 *
 * native 仍接受 2=HANDOFF，但 approval-status §3 已裁 Kotlin 不发；本闭集
 * 刻意不含 2，防线只在 Kotlin 枚举上。
 */
enum class Flow43499FinishAction(val wire: Int) {
    RETIRE(0),
    PARK(1),
}

/** native `FinishVictim43499` 的两个字段。 */
data class Flow43499FinishVictim(
    val action: Flow43499FinishAction,
    val victimGeneration: Long,
)
