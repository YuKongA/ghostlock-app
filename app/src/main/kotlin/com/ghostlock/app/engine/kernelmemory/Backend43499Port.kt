package com.ghostlock.app.engine.kernelmemory

import com.ghostlock.app.core.generated.Discover43499
import com.ghostlock.app.core.generated.Prepare43499

/**
 * 43499 生产引擎的原生边界（K1）。
 *
 * 引擎只认这里的具名结果，不认 JNI DTO 字段顺序、不认 LongArray 下标。
 * 真实实现见 [JniBackend43499Port]；测试用 fake。
 *
 * 生命周期：create → initialize → prepare → discover → spawnVictim → writeStep* → victimProbe → finishVictim → close。
 * 每个阶段失败都返回 [PortResult.Refused]，调用方据此停手；不得把拒绝折成成功。
 */
interface Backend43499Port {
    /** 当前 native 面能证明的能力；未证明的一律 false（fail-closed）。 */
    val capabilities: NativeCapabilities

    fun create(): PortResult<Unit>

    fun initialize(session: KernelSession): PortResult<Unit>

    fun prepare(request: Prepare43499): PortResult<Facts43499>

    fun discover(request: Discover43499): PortResult<Facts43499>

    fun spawnVictim(): PortResult<Facts43499>

    fun victimProbe(kind: Int): PortResult<Long>

    fun finishVictim(action: Int, victimGeneration: Long): PortResult<Facts43499>

    /**
     * 一次写事务：访问对象只在本次调用内存在。
     *
     * @param step 本步的 route/offset/address/target/value（target/value 是 route 的守卫对）。
     * @param address 写入地址（通常等于 step.address；保留显式参数以便对拍 StepPlans）。
     * @param value 写入值（W1/W3 为 0，W2 为凭据值）。
     * @param form 1=保留清零，2=叶子清零，3=写值保留。
     * @param offset route 内偏移。
     */
    fun writeStep(step: KernelStep, address: Long, value: Long, form: Int, offset: Long): PortResult<Unit>

    fun close(): PortResult<Unit>
}

/** 一次调用的结局：成功只回答值，失败只回答具名拒绝。 */
sealed interface PortResult<out T> {
    data class Ok<T>(val value: T) : PortResult<T>
    data class Refused(val refusal: PortRefusal) : PortResult<Nothing>
}

/** 具名拒绝：code 是调用码的名字，detail 是 native 的 refusal（或能力门的原因）。 */
data class PortRefusal(val code: PortCode, val detail: String) {
    init {
        require(detail.isNotBlank()) { "refusal detail must not be blank" }
    }

    override fun toString(): String = code.token + ":" + detail
}

/**
 * 调用码（与 native CallCode 一一对应；未知码由适配器折成 FAILED，不猜成功）。
 *
 * IN_FLIGHT 在步骤引擎里映射为 indeterminate：效果不确定 ⇒ 冻结，不得重试。
 */
enum class PortCode(val token: String) {
    NOT_READY("not-ready"),
    BAD_PARAMETERS("bad-parameters"),
    UNSUPPORTED("unsupported"),
    FAILED("failed"),
    STALE_HANDLE("stale-handle"),
    BUSY("busy"),
    STALE_TARGET("stale-target"),
    IN_FLIGHT("in-flight"),
    CLOSED("closed"),
}

/**
 * native 能力门。
 *
 * - [generations]：snapshot 是否发布 prepare/discovery/victim 三代；
 * - [formPropagation]：写调用是否把 form 传到设备写；
 * - [perStepTargetValue]：写调用是否能携带本步 target/value。
 *
 * K1 的 native 面对后两项没有探针，适配器保守置 false；native 计划补显式探针后再打开。
 */
data class NativeCapabilities(
    val generations: Boolean,
    val formPropagation: Boolean,
    val perStepTargetValue: Boolean,
)
