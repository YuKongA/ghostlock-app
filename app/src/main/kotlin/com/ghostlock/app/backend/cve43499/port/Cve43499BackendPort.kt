package com.ghostlock.app.backend.cve43499.port

import com.ghostlock.app.backend.cve43499.flow.Flow43499FinishAction
import com.ghostlock.app.backend.cve43499.flow.Flow43499Request
import com.ghostlock.app.backend.cve43499.flow.Flow43499WordWrite

/**
 * Flow43499Impl 与 Native43499 之间的唯一中转层。
 *
 * Flow 只认这里的具名结果，不直接调用 JNI，不认 LongArray 下标或 native DTO
 * 顺序。Port 拥有 native session/handle、typed 资源和状态；Flow 只拥有固定
 * 步骤顺序、重试与 cleanup。
 *
 * 生命周期：create → initialize → prepare → discover → spawnVictim →
 * writeStep* → victimProbe → finishVictim → shutdown。任一阶段拒绝即停手。
 */
interface Cve43499BackendPort {
    /** 当前 native 面能证明的能力；未证明的一律 false。 */
    val capabilities: Cve43499Capabilities

    suspend fun create(): Cve43499PortResult<Unit>

    suspend fun initialize(request: Flow43499Request): Cve43499PortResult<Unit>

    suspend fun prepare(request: Flow43499Request.Prepare): Cve43499PortResult<Unit>

    suspend fun discover(
        request: Flow43499Request.Discover,
        prepareGeneration: Long,
    ): Cve43499PortResult<Unit>

    suspend fun spawnVictim(): Cve43499PortResult<Unit>

    suspend fun victimProbe(kind: Int): Cve43499PortResult<Long>

    suspend fun writeStep(word: Flow43499WordWrite): Cve43499PortResult<Unit>

    suspend fun finishVictim(
        action: Flow43499FinishAction,
        victimGeneration: Long,
    ): Cve43499PortResult<Unit>

    suspend fun shutdown(): Cve43499PortResult<Unit>
}

/** 建立一次 43499 port；配置在 factory 构造时绑定。 */
fun interface Cve43499BackendFactory {
    suspend fun open(): Cve43499PortResult<Cve43499BackendPort>
}

/** Port 一次调用的结局；拒绝必须带具名 reason。 */
sealed interface Cve43499PortResult<out T> {
    data class Ok<T>(val value: T) : Cve43499PortResult<T>
    data class Refused(val reason: String) : Cve43499PortResult<Nothing>
}

/** native 能力门；未证明的项一律 false。 */
data class Cve43499Capabilities(
    val generations: Boolean = false,
    val formPropagation: Boolean = false,
    val perStepTargetValue: Boolean = false,
)
