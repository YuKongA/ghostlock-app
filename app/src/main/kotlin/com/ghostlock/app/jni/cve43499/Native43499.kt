package com.ghostlock.app.jni.cve43499

import com.ghostlock.app.core.generated.CredentialLayout43499
import com.ghostlock.app.core.generated.Discover43499
import com.ghostlock.app.core.generated.FinishVictim43499
import com.ghostlock.app.core.generated.Init43499
import com.ghostlock.app.core.generated.KernelLayout43499
import com.ghostlock.app.core.generated.Prepare43499
import com.ghostlock.app.core.generated.TargetOffsets43499
import com.ghostlock.app.core.generated.WordWrite43499

/**
 * libghostlock43499.so 的函数面（D2 装配，2026-10-15）。
 *
 * 与旧边界的区别：**没有 op 名、没有 token、没有字节帧**。每个函数都是普通函数，参数是生成的 DTO
 * 对象（D1 单表）或数字，返回值是调用码；具名拒绝与调用码一次写回 refusalOut[0]（长度 ≥ 1 的数组），
 * 没有"再单独问一次 refusal(handle)"的窗口。
 *
 * 调用码（与 native backend_api.hpp 的 CallCode 一一对应，顺序即数值）：
 *   0 ok、-1 not-ready、-2 bad-parameters、-3 unsupported、-4 failed、-5 stale-handle、
 *   -6 busy、-7 stale-target、-8 inflight、-9 closed。
 */
object Native43499 {
    /** 这个 backend 的库名（CMake 目标 ghostlock43499）。 */
    const val LIBRARY = "ghostlock43499"

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

    /** 建一个会话。@return 句柄（> 0）；0 表示没有句柄。 */
    external fun create(): Long

    /** 初始化：四组 DTO 一起给，缺字段按名拒绝（拒绝名在 refusalOut[0]）。 */
    external fun initialize(
        handle: Long,
        init: Init43499,
        layout: KernelLayout43499,
        credential: CredentialLayout43499,
        targets: TargetOffsets43499,
        refusalOut: Array<String>,
    ): Int

    external fun snapshot(handle: Long, out: LongArray, refusalOut: Array<String>): Int

    external fun prepare(handle: Long, request: Prepare43499, out: LongArray, refusalOut: Array<String>): Int

    external fun discover(handle: Long, request: Discover43499, out: LongArray, refusalOut: Array<String>): Int

    external fun spawnVictim(handle: Long, out: LongArray, refusalOut: Array<String>): Int

    external fun finishVictim(
        handle: Long,
        request: FinishVictim43499,
        out: LongArray,
        refusalOut: Array<String>,
    ): Int

    external fun victimProbe(handle: Long, kind: Int, out: LongArray, refusalOut: Array<String>): Int

    external fun writeWord(handle: Long, request: WordWrite43499, refusalOut: Array<String>): Int

    external fun readWord(handle: Long, address: Long, out: LongArray, refusalOut: Array<String>): Int

    external fun shutdown(handle: Long, refusalOut: Array<String>): Int

    external fun destroy(handle: Long, refusalOut: Array<String>): Int

    /** @return snapshot 结果数组每一位的名字（第几位是什么，由 native 导出）。 */
    /**
     *  按 route 建一个内核访问对象（route 由调用方从档案取值）。
     *
     *  @param out 一个 long：访问对象句柄；失败时 refusalOut[0] 为 access-refused。
     */
    external fun makeAccess43499(handle: Long, route: Int, out: LongArray, refusalOut: Array<String>): Int

    /** 拆掉一个内核访问对象。 */
    external fun destroyAccess43499(handle: Long, accessId: Long, refusalOut: Array<String>): Int

    external fun snapshotFields(): Array<String>

    /** 初始化（路由与位置由调用方给）；失败时 refusalOut[0] 为 init:<原因>。 */
    external fun initialize43499(
        handle: Long,
        accessId: Long,
        route: Int,
        offset: Long,
        offsetSet: Boolean,
        refusalOut: Array<String>,
    ): Int
    /**
     *  Initialize carrying the assembly (new entry point; the old one is untouched).
     *
     *  assembly order, seventeen values decided in Kotlin:
     *  waiter, task, lock, buffer, maxAttempts, armHold, armSequence, parkMs, settleUs,
     *  consumerDelayUs, burstCalls, maxCalls, fallbackLockUs, niceModulus, policy, fakeTask, fakeLock.
     *  A shorter array falls back to the old behaviour.
     */
    external fun initialize43499Full(
        handle: Long,
        accessId: Long,
        route: Int,
        offset: Long,
        offsetSet: Boolean,
        assembly: LongArray,
        refusalOut: Array<String>,
    ): Int

    /** 写一块（通道 PiWindow）；失败时 refusalOut[0] 为 write:<原因>。 */
    external fun write43499(
        handle: Long,
        accessId: Long,
        route: Int,
        offset: Long,
        address: Long,
        block: ByteArray,
        refusalOut: Array<String>,
    ): Int

    /** 读一块（通道 PiWindow）；读回字节进 out，实际长度进 outLen[0]。 */
    external fun read43499(
        handle: Long,
        accessId: Long,
        route: Int,
        offset: Long,
        address: Long,
        bytes: Int,
        out: ByteArray,
        outLen: LongArray,
        refusalOut: Array<String>,
    ): Int

    /** 关掉访问对象（收尾第一步）；失败时 refusalOut[0] 为 shutdown:<原因>。 */
    external fun shutdown43499(handle: Long, accessId: Long, refusalOut: Array<String>): Int
}
