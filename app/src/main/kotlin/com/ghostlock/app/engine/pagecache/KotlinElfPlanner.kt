package com.ghostlock.app.engine.pagecache

import com.ghostlock.app.engine.pagecache.stage.StageLog
import com.ghostlock.app.engine.pagecache.stage.silentStageLog

/** ELF64 区间；所有字段在返回前都做过越界校验。 */
internal data class ElfRange(val fileOffset: Long, val size: Long, val vaddr: Long)

/** 一个 section 的最小事实。 */
internal data class ElfSection(
    val name: String,
    val type: Long,
    val flags: Long,
    val vaddr: Long,
    val fileOffset: Long,
    val size: Long,
    val link: Int,
    val entsize: Long,
)

internal data class ElfLayout(
    val exec: ElfRange?,
    val execMemSize: Long,
    val execAlign: Long,
    val sections: List<ElfSection>,
) {
    val dynsym: ElfSection? get() = sections.firstOrNull { it.name == ".dynsym" }
    val dynstr: ElfSection? get() = sections.firstOrNull { it.name == ".dynstr" }
    val relaPlt: ElfSection? get() = sections.firstOrNull { it.name == ".rela.plt" }
    val relaDyn: ElfSection? get() = sections.firstOrNull { it.name == ".rela.dyn" }
}

internal data class ElfSymbol(
    val name: String,
    val value: Long,
    val size: Long,
    val sectionIndex: Int,
    val fileOffset: Long,
    val section: ElfSection,
)

/** Kotlin 计算出的 sentry plan。 */
data class KotlinSentryPlan(
    val siteFile: Long,
    val siteVaddr: Long,
    val landingFile: Long,
    val landingVaddr: Long,
    val payloadMax: Long,
    val displaced: Int,
    val guard: Int,
    val branchWord: Int,
    val jumpBackWord: Int,
    val entryOffset: Int = 0x90,
    val displacedOffset: Int = 0x1D0,
    val jumpBackOffset: Int = 0x1D4,
)

/**
 * 从目标 ELF 现场派生 hook plan。
 *
 * 全部逻辑在 Kotlin：解析 ehdr/phdr/shdr、查 dynsym、处理 guard、校验 relocation、
 * 搜索 code cave、编码 AArch64 B。C++/JNI 不参与。
 */
object KotlinElfPlanner {
    private const val ELF_MAGIC = 0x464C457F
    private const val ET_EXEC = 2
    private const val ET_DYN = 3
    private const val EM_AARCH64 = 0xB7
    private const val PT_LOAD = 1L
    private const val PF_X = 0x1L
    private const val SHF_ALLOC = 0x2L
    private const val SHF_EXECINSTR = 0x4L
    private const val SHT_ANDROID_RELA = 0x60000001L
    private const val SENTRY =
        "_ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE6sentryC1ERS3_"

    fun plan(target: ElfTarget, payload: ByteArray, log: StageLog = silentStageLog): KotlinSentryPlan? {
        log.verbose("KotlinElfPlanner.plan", "target" to target.path, "payload" to payload.size)
        val layout = parse(target.bytes)
        if (layout == null) {
            log.debug("KotlinElfPlanner.plan", "layout-null")
            return null
        }
        val exec = layout.exec ?: return null
        val symbol = findSymbol(target.bytes, layout, SENTRY) ?: return null
        val guard = readU32(target.bytes, symbol.fileOffset.toInt())?.toInt() ?: return null
        val guarded = isGuard(guard)
        val siteFile = symbol.fileOffset + if (guarded) 4 else 0
        val siteVaddr = symbol.value + if (guarded) 4 else 0
        val displaced = readU32(target.bytes, siteFile.toInt())?.toInt() ?: return null
        val padded = alignUp(payload.size.toLong(), 16L)
        val landing = findLanding(target.bytes, layout, siteVaddr, padded) ?: return null
        val payloadMax = landingRoom(target.bytes, layout, landing) ?: return null
        if (padded > payloadMax) return null
        val entry = landing + 0x90L
        val branch = encodeBranch(siteVaddr, entry, log)
        if (branch == null) {
            log.debug("KotlinElfPlanner.plan", "branch-null", "site" to siteVaddr, "entry" to entry)
            return null
        }
        val jumpBack = encodeBranch(landing + 0x1D4L, siteVaddr + 4L, log)
        if (jumpBack == null) {
            log.debug("KotlinElfPlanner.plan", "jump-null", "landing" to landing, "site" to siteVaddr)
            return null
        }
        return KotlinSentryPlan(
            siteFile = siteFile,
            siteVaddr = siteVaddr,
            landingFile = landing,
            landingVaddr = exec.vaddr + (landing - exec.fileOffset),
            payloadMax = payloadMax,
            displaced = displaced,
            guard = if (guarded) guard else 0,
            branchWord = branch,
            jumpBackWord = jumpBack,
        )
    }

    /**
     * 静态 ELF 的入口钩子：site = e_entry，landing = 可执行段末尾页尾/零填充。
     * 用于没有 libc++ sentry 可用的静态二进制（例如模拟器的 crash_dump64）。
     */
    fun planEntry(target: ElfTarget, payload: ByteArray, log: StageLog = silentStageLog): KotlinSentryPlan? {
        log.verbose("KotlinElfPlanner.planEntry", "target" to target.path, "payload" to payload.size)
        val layout = parse(target.bytes) ?: return null
        val exec = layout.exec ?: return null
        val entry = readU64(target.bytes, 0x18) ?: return null
        if (entry < exec.vaddr || entry + 4L > exec.vaddr + exec.size) return null
        val siteFile = exec.fileOffset + (entry - exec.vaddr)
        val displaced = readU32(target.bytes, siteFile.toInt())?.toInt() ?: return null
        val padded = alignUp(payload.size.toLong(), 16L)
        val landing = findLanding(target.bytes, layout, entry, padded) ?: return null
        val payloadMax = landingRoom(target.bytes, layout, landing) ?: return null
        if (padded > payloadMax) return null
        val branch = encodeBranch(entry, landing + 0x90L, log) ?: return null
        val jumpBack = encodeBranch(landing + 0x1D4L, entry + 4L, log) ?: return null
        log.debug("KotlinElfPlanner.planEntry", "ok", "site" to entry, "landing" to landing)
        return KotlinSentryPlan(
            siteFile = siteFile,
            siteVaddr = entry,
            landingFile = landing,
            landingVaddr = exec.vaddr + (landing - exec.fileOffset),
            payloadMax = payloadMax,
            displaced = displaced,
            guard = 0,
            branchWord = branch,
            jumpBackWord = jumpBack,
        )
    }

    internal fun parse(bytes: ByteArray): ElfLayout? {
        if (bytes.size < 64 || readU32(bytes, 0) != ELF_MAGIC.toLong()) return null
        if (bytes[4].toInt() != 2 || bytes[5].toInt() != 1) return null
        val type = readU16(bytes, 0x10) ?: return null
        if (type != ET_EXEC && type != ET_DYN) return null
        if (readU16(bytes, 0x12) != EM_AARCH64) return null
        val phoff = readU64(bytes, 0x20) ?: return null
        val shoff = readU64(bytes, 0x28) ?: return null
        val phentsize = readU16(bytes, 0x36) ?: return null
        val phnum = readU16(bytes, 0x38) ?: return null
        val shentsize = readU16(bytes, 0x3A) ?: return null
        val shnum = readU16(bytes, 0x3C) ?: return null
        val shstrndx = readU16(bytes, 0x3E) ?: return null
        if (phentsize < 56 || shentsize < 64) return null
        if (!rangeOk(phoff, phnum.toLong() * phentsize, bytes.size)) return null
        if (!rangeOk(shoff, shnum.toLong() * shentsize, bytes.size)) return null
        if (shstrndx >= shnum) return null

        val sectionHeaders = ArrayList<RawSection>(shnum)
        for (index in 0 until shnum) {
            val base = shoff + index.toLong() * shentsize
            val raw = RawSection(
                nameOffset = readU32(bytes, base.toInt()) ?: return null,
                type = readU32(bytes, base.toInt() + 4) ?: return null,
                flags = readU64(bytes, base.toInt() + 8) ?: return null,
                vaddr = readU64(bytes, base.toInt() + 16) ?: return null,
                fileOffset = readU64(bytes, base.toInt() + 24) ?: return null,
                size = readU64(bytes, base.toInt() + 32) ?: return null,
                link = readU32(bytes, base.toInt() + 40)?.toInt() ?: return null,
                entsize = readU64(bytes, base.toInt() + 56) ?: return null,
            )
            /* SHT_NOBITS（.bss）在文件里没有字节，sh_size 是内存大小。 */
            if (raw.type != 8L && !rangeOk(raw.fileOffset, raw.size, bytes.size)) return null
            sectionHeaders += raw
        }
        val shstr = sectionHeaders[shstrndx]
        val names = ByteArray(shstr.size.toInt())
        System.arraycopy(bytes, shstr.fileOffset.toInt(), names, 0, names.size)
        fun nameAt(offset: Long): String {
            if (offset < 0 || offset >= names.size) return ""
            var end = offset.toInt()
            while (end < names.size && names[end] != 0.toByte()) end += 1
            return String(names, offset.toInt(), end - offset.toInt(), Charsets.UTF_8)
        }
        val sections = sectionHeaders.map { raw ->
            ElfSection(nameAt(raw.nameOffset), raw.type, raw.flags, raw.vaddr, raw.fileOffset, raw.size, raw.link, raw.entsize)
        }

        var exec: ElfRange? = null
        var execMem = 0L
        var execAlign = 0L
        for (index in 0 until phnum) {
            val base = phoff + index.toLong() * phentsize
            val pType = readU32(bytes, base.toInt()) ?: return null
            val pFlags = readU32(bytes, base.toInt() + 4) ?: return null
            if (pType != PT_LOAD || (pFlags and PF_X) == 0L) continue
            val fileOffset = readU64(bytes, base.toInt() + 8) ?: return null
            val vaddr = readU64(bytes, base.toInt() + 16) ?: return null
            val fileSize = readU64(bytes, base.toInt() + 32) ?: return null
            val memSize = readU64(bytes, base.toInt() + 40) ?: return null
            val align = readU64(bytes, base.toInt() + 48) ?: return null
            if (!rangeOk(fileOffset, fileSize, bytes.size)) return null
            exec = ElfRange(fileOffset, fileSize, vaddr)
            execMem = memSize
            execAlign = align
            break
        }
        return ElfLayout(exec, execMem, execAlign, sections)
    }

    internal fun findSymbol(bytes: ByteArray, layout: ElfLayout, name: String): ElfSymbol? {
        val dynsym = layout.dynsym ?: return null
        val dynstr = layout.dynstr ?: return null
        if (dynsym.entsize != 24L || dynsym.size % 24L != 0L) return null
        val count = dynsym.size / 24L
        for (index in 0 until count) {
            val base = dynsym.fileOffset + index * 24L
            val nameOffset = readU32(bytes, base.toInt()) ?: return null
            if (nameOffset >= dynstr.size) continue
            val symbolName = cString(bytes, dynstr.fileOffset + nameOffset, dynstr.size - nameOffset) ?: continue
            if (symbolName != name) continue
            val info = bytes[base.toInt() + 4].toInt() and 0xFF
            val sectionIndex = readU16(bytes, base.toInt() + 6) ?: return null
            if (sectionIndex == 0 || sectionIndex >= layout.sections.size) return null
            val section = layout.sections[sectionIndex]
            if ((section.flags and (SHF_ALLOC or SHF_EXECINSTR)) != (SHF_ALLOC or SHF_EXECINSTR)) return null
            val value = readU64(bytes, base.toInt() + 8) ?: return null
            val size = readU64(bytes, base.toInt() + 16) ?: return null
            if (value < section.vaddr || value + size > section.vaddr + section.size) return null
            val fileOffset = section.fileOffset + (value - section.vaddr)
            return ElfSymbol(symbolName, value, size, sectionIndex, fileOffset, section)
        }
        return null
    }

    fun encodeBranch(from: Long, to: Long, log: StageLog = silentStageLog): Int? {
        log.verbose("KotlinElfPlanner.encodeBranch", "from" to from, "to" to to)
        if (((from or to) and 0x3L) != 0L) {
            log.debug("KotlinElfPlanner.encodeBranch", "unaligned", "from" to from, "to" to to)
            return null
        }
        val delta = to - from
        if (delta % 4L != 0L) {
            log.debug("KotlinElfPlanner.encodeBranch", "delta-unaligned", "delta" to delta)
            return null
        }
        val words = delta / 4L
        if (words < -(1L shl 25) || words >= (1L shl 25)) {
            log.debug("KotlinElfPlanner.encodeBranch", "out-of-range", "words" to words)
            return null
        }
        val encoded = 0x14000000.toInt() or (words.toInt() and 0x03FFFFFF)
        log.debug("KotlinElfPlanner.encodeBranch", "ok", "from" to from, "to" to to)
        return encoded
    }

    /** 站点所在段末尾的页尾；容量按 payload 需求裁剪。 */
    private fun payloadRoom(bytes: ByteArray, layout: ElfLayout, padded: Long): Long? {
        val exec = layout.exec ?: return null
        val end = exec.fileOffset + exec.size
        if (end >= bytes.size) return 0L
        val pageEnd = alignUp(end, 0x1000L)
        return (minOf(pageEnd, bytes.size.toLong()) - end).coerceAtLeast(0L)
    }

    /** 已选 landing 的实际容量：段尾页尾取页尾，段内 cave 取到段末。 */
    private fun landingRoom(bytes: ByteArray, layout: ElfLayout, landing: Long): Long? {
        val exec = layout.exec ?: return null
        val end = exec.fileOffset + exec.size
        return if (landing >= end) {
            (minOf(alignUp(end, 0x1000L), bytes.size.toLong()) - landing).coerceAtLeast(0L)
        } else {
            (end - landing).coerceAtLeast(0L)
        }
    }

    /** 候选 cave：优先段尾页尾；不足时扫描段内零填充。 */
    private fun findLanding(bytes: ByteArray, layout: ElfLayout, siteVaddr: Long, padded: Long): Long? {
        val exec = layout.exec ?: return null
        val end = exec.fileOffset + exec.size
        val pageEnd = alignUp(end, 0x1000L)
        val tail = minOf(pageEnd, bytes.size.toLong()) - end
        if (tail >= padded && !overlapsRelocation(bytes, layout, exec.vaddr + (end - exec.fileOffset), padded)) {
            return end
        }
        var cursor = alignUp(exec.fileOffset, 16L)
        val limit = end - padded
        while (cursor <= limit) {
            if (!overlapsRelocation(bytes, layout, exec.vaddr + (cursor - exec.fileOffset), padded) &&
                isZeroRun(bytes, cursor, padded)
            ) {
                return cursor
            }
            cursor += 16L
        }
        return null
    }

    private fun isZeroRun(bytes: ByteArray, offset: Long, length: Long): Boolean {
        if (!rangeOk(offset, length, bytes.size)) return false
        for (index in 0 until length.toInt()) {
            if (bytes[offset.toInt() + index] != 0.toByte()) return false
        }
        return true
    }

    private fun overlapsRelocation(bytes: ByteArray, layout: ElfLayout, vaddr: Long, length: Long): Boolean {
        val tables = listOfNotNull(
            layout.relaPlt?.takeIf { it.entsize == 24L && it.size % 24L == 0L },
            layout.relaDyn?.takeIf { it.type != SHT_ANDROID_RELA && it.entsize == 24L && it.size % 24L == 0L },
        )
        for (table in tables) {
            val count = table.size / 24L
            for (index in 0 until count) {
                val offset = readU64(bytes, (table.fileOffset + index * 24L).toInt()) ?: return true
                if (rangesOverlap(vaddr, length, offset, 8L)) return true
            }
        }
        return false
    }

    private fun isGuard(word: Int): Boolean =
        word == 0xD503233F.toInt() || word == 0xD503237F.toInt() ||
            word == 0xD503245F.toInt() || word == 0xD503249F.toInt() ||
            word == 0xD50324DF.toInt()

    private fun cString(bytes: ByteArray, offset: Long, max: Long): String? {
        if (offset < 0 || offset >= bytes.size || max <= 0) return null
        var end = offset.toInt()
        val limit = minOf(bytes.size.toLong(), offset + max).toInt()
        while (end < limit && bytes[end] != 0.toByte()) end += 1
        if (end == limit) return null
        return String(bytes, offset.toInt(), end - offset.toInt(), Charsets.UTF_8)
    }

    private fun rangesOverlap(a: Long, alen: Long, b: Long, blen: Long): Boolean =
        a < b + blen && b < a + alen

    private fun rangeOk(offset: Long, size: Long, total: Int): Boolean =
        offset >= 0L && size >= 0L && offset <= total.toLong() && size <= total.toLong() - offset

    private fun alignUp(value: Long, alignment: Long): Long =
        if (alignment <= 1L) value else (value + alignment - 1L) / alignment * alignment

    private fun readU16(bytes: ByteArray, at: Int): Int? {
        if (at < 0 || at + 2 > bytes.size) return null
        return (bytes[at].toInt() and 0xFF) or ((bytes[at + 1].toInt() and 0xFF) shl 8)
    }

    private fun readU32(bytes: ByteArray, at: Int): Long? {
        if (at < 0 || at + 4 > bytes.size) return null
        return (bytes[at].toLong() and 0xFF) or
            ((bytes[at + 1].toLong() and 0xFF) shl 8) or
            ((bytes[at + 2].toLong() and 0xFF) shl 16) or
            ((bytes[at + 3].toLong() and 0xFF) shl 24)
    }

    private fun readU64(bytes: ByteArray, at: Int): Long? {
        if (at < 0 || at + 8 > bytes.size) return null
        var value = 0L
        for (index in 0 until 8) {
            value = value or ((bytes[at + index].toLong() and 0xFF) shl (8 * index))
        }
        return value
    }

    private data class RawSection(
        val nameOffset: Long,
        val type: Long,
        val flags: Long,
        val vaddr: Long,
        val fileOffset: Long,
        val size: Long,
        val link: Int,
        val entsize: Long,
    )
}
