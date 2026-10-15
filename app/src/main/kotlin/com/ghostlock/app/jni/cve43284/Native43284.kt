package com.ghostlock.app.jni.cve43284

import com.ghostlock.app.core.generated.HookPlanRequest43284
import com.ghostlock.app.core.generated.Init43284
import com.ghostlock.app.core.generated.ModuleCheck43284
import com.ghostlock.app.core.generated.ReadBlock43284
import com.ghostlock.app.core.generated.ResolveTarget43284
import com.ghostlock.app.core.generated.Trigger43284
import com.ghostlock.app.core.generated.VerifyTarget43284
import com.ghostlock.app.core.generated.Wait43284
import com.ghostlock.app.core.generated.WriteBlock43284

/**
 * libghostlock43284.so 的函数面（D2 装配，2026-10-15）。
 *
 * 参数是 D1 生成的 DTO（单表权威），返回值是调用码；具名拒绝与调用码一次写回 refusalOut[0]。
 * 调用码与 native 的 CallCode 一一对应：0 ok、-1 not-ready、-2 bad-parameters、-3 unsupported、
 * -4 failed、-5 stale-handle、-6 busy、-7 stale-target、-8 inflight、-9 closed。
 */
object Native43284 {
    /** 这个 backend 的库名（CMake 目标 ghostlock43284）。 */
    const val LIBRARY = "ghostlock43284"

    private var loaded = false

    /** @return 库能不能加载；失败时 false（调用方据此给具名拒绝，而不是抛异常）。 */
    fun ensureLoaded(): Boolean {
        if (loaded) {
            return true
        }
        loaded = try {
            System.loadLibrary(LIBRARY)
            true
        } catch (missing: UnsatisfiedLinkError) {
            false
        }
        return loaded
    }

    external fun create(): Long

    /** 初始化：只建立安全关联（SA/peer/socket），门由 resolveTarget 创建。 */
    external fun initialize(handle: Long, params: Init43284, refusalOut: Array<String>): Int

    external fun snapshot(handle: Long, out: LongArray, refusalOut: Array<String>): Int

    /** 绑定一个角色到一条路径。@param out 一个 long：票据。 */
    external fun resolveTarget(
        handle: Long,
        request: ResolveTarget43284,
        out: LongArray,
        refusalOut: Array<String>,
    ): Int

    /** 校验目标内容（摘要 + 大小）⇒ Verified。 */
    external fun verifyTarget(handle: Long, request: VerifyTarget43284, refusalOut: Array<String>): Int

    /** 写一个十六字节块 ⇒ Dispatched（不是改过了）。 */
    external fun writeBlock(handle: Long, request: WriteBlock43284, refusalOut: Array<String>): Int

    /** 读回一个十六字节块；一致时该 slot 推到 Applied。 */
    external fun readBlock(
        handle: Long,
        request: ReadBlock43284,
        out: ByteArray,
        refusalOut: Array<String>,
    ): Int

    external fun moduleCheck(handle: Long, request: ModuleCheck43284, refusalOut: Array<String>): Int

    /** 动态钩子计划。@param out 四个 long：branch、displaced、site、landing。 */
    external fun hookPlan(
        handle: Long,
        request: HookPlanRequest43284,
        out: LongArray,
        refusalOut: Array<String>,
    ): Int

    external fun trigger(handle: Long, request: Trigger43284, refusalOut: Array<String>): Int

    /** 等终态。@param out 一个 long：0 failure / 1 success / 2 inflight。 */
    external fun waitTerminal(
        handle: Long,
        request: Wait43284,
        out: LongArray,
        refusalOut: Array<String>,
    ): Int

    /** @return 本构建自带的字节；失败时 null（名字在 refusalOut[0]）。 */
    external fun embeddedImage(handle: Long, role: Int, refusalOut: Array<String>): ByteArray?

    /** @return 钩子要写的载荷；失败时 null。 */
    external fun hookPayload(handle: Long, refusalOut: Array<String>): ByteArray?

    external fun shutdown(handle: Long, refusalOut: Array<String>): Int

    external fun destroy(handle: Long, refusalOut: Array<String>): Int

    /** @param out 一个 long：Access 句柄；失败时 refusalOut[0] 为 access-refused。 */
    external fun makeAccess(handle: Long, landingId: Long, out: LongArray, refusalOut: Array<String>): Int

    /** 拆掉一个 Access。 */
    external fun destroyAccess(handle: Long, accessId: Long, refusalOut: Array<String>): Int

    /** 往落地对象写一块（十六字节）；失败时 refusalOut[0] 为 block-refused。 */
    external fun landBlock(handle: Long, landingId: Long, offset: Long, block: ByteArray, refusalOut: Array<String>): Int

    /** 从落地对象读回一块（十六字节）；失败时 refusalOut[0] 为 block-read-refused。 */
    external fun readLandingBlock(handle: Long, landingId: Long, offset: Long, out: ByteArray, refusalOut: Array<String>): Int

    /** 用会话参数把落地对象拉起来；失败时 refusalOut[0] 为 bring-up-refused。 */
    external fun bringUp(handle: Long, landingId: Long, params: Init43284, refusalOut: Array<String>): Int

    /** @param out 一个 long：窗口的下一个序号。 */
    external fun nextSequence(handle: Long, landingId: Long, out: LongArray, refusalOut: Array<String>): Int

    /** 让落地对象采用下一个序号（跨窗口接续也走这里）。 */
    external fun adoptSequence(handle: Long, landingId: Long, next: Long, refusalOut: Array<String>): Int

    /** @param out 一个 long：socket 句柄（失败时不写）；失败时 refusalOut[0] 为 esp-refused。 */
    external fun espSocketId(handle: Long, params: Init43284, out: LongArray, refusalOut: Array<String>): Int

    /** 按 id 关 socket（谁建谁关）。 */
    external fun closeSocketId(handle: Long, socketId: Long, refusalOut: Array<String>): Int

    /** @param out 一个 long：拼接面句柄。 */
    external fun makeSurface(handle: Long, targetId: Long, socketId: Long, out: LongArray, refusalOut: Array<String>): Int

    /** 拆拼接面（连它的落地对象一起）。 */
    external fun destroySurface(handle: Long, surfaceId: Long, refusalOut: Array<String>): Int

    /** @param out 一个 long：落地对象句柄。 */
    external fun makeLanding(handle: Long, surfaceId: Long, out: LongArray, refusalOut: Array<String>): Int

    /** 拆落地对象。 */
    external fun destroyLanding(handle: Long, landingId: Long, refusalOut: Array<String>): Int

    /** 建 ESP socket：0 成功；失败时 refusalOut[0] 为 esp-refused。 */
    external fun connectEsp(handle: Long, params: Init43284, refusalOut: Array<String>): Int

    /** 关掉一个 socket。 */
    external fun closeSocket(handle: Long, fd: Int, refusalOut: Array<String>): Int

    /** 关掉一扇门（谁开谁关）：id 无效时调用码为 -7（stale-target）。 */
    external fun closeTarget(handle: Long, targetId: Long, refusalOut: Array<String>): Int

    /** @return snapshot 结果数组每一位的名字（由 native 导出）。 */
    external fun snapshotFields(): Array<String>
}
