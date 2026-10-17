package com.ghostlock.app.jni.cve43284

/**
 * vermagic 的构造、判型与原地重写（Kotlin 侧唯一权威）。
 *
 * 参照 `ghostlock-app-ref@5f184760` 的
 * `src/core/backend/cve_2026_43284/lkm/lkm_image.cpp`：
 *  - `uts_release_token` :370-386；
 *  - `required_vermagic` :388-412；
 *  - `append_vermagic_token` :236-250（token 间恰好一个空格，空 token 不写）；
 *  - `classify_vermagic_diff` :219-234（None/Release/Options/Unparsable）；
 *  - `rewrite_vermagic` :574-621（事务性：一切可能失败都在第一次写入之前；
 *    放不下 ⇒ `VermagicSlotTooSmall`；写后清零槽内余量）。
 *
 * 纯函数：不读环境、不写全局、不共享可变状态。`rewrite` 的入参始终只读；
 * 成功时返回新数组，失败时入参一个字节都不变。
 */
object ModuleVermagic43284 {
    /** vermagic 判型（与 ref `VermagicDiffReason` 一一对应）。 */
    enum class Verdict {
        /** 整串相同（ref `VermagicDiffReason::None`）。 */
        NONE,

        /** 仅 release token 不同，首个空格之后的选项尾串相同（ref `Release`）。 */
        RELEASE,

        /** 首个空格之后的选项尾串不同（ref `Options`）。 */
        OPTIONS,

        /** 任一输入为空，无法判定（ref `Unparsable`）。 */
        UNPARSABLE,
    }

    /** `rewrite` 的具名结果（不抛异常，不部分写入）。 */
    sealed interface RewriteResult {
        /**
         * 改写成功。
         *
         * 对应 ref `VermagicOutcome::Rewritten`（`vermagic_outcome_name` :360-366
         * 输出 `rewritten`）。[bytes] 是新数组，入参从未被修改。
         */
        class Rewritten(val bytes: ByteArray) : RewriteResult

        /**
         * 旧条目槽放不下新的 `vermagic=` 条目。
         *
         * @param need 完整新条目所需字节：`"vermagic=" + required + NUL`。
         * @param have 旧条目槽字节：从条目起点到旧 NUL（含 NUL）。
         */
        data class SlotTooSmall(val need: Int, val have: Int) : RewriteResult

        /**
         * 没有可改写的载体：`.modinfo` 不存在、其中没有 `vermagic=` 条目，或
         * [rewrite] 的 `required` 为空。
         *
         * ref 对应 `MissingModinfo`（:584-588）与 `VermagicMissing`（:575-578 / :619）。
         */
        data object MissingModinfo : RewriteResult

        /** ELF64/AArch64 信封或段头表不可用（ref :579-583 与 `find_section` :90-143）。 */
        data object BadElf : RewriteResult
    }

    /**
     * 构造 required vermagic：逐 token 拼接，顺序与 ref 完全一致
     * （`required_vermagic` :388-412）：
     * `release, SMP, [preempt], [mod_unload], [modversions], aarch64`。
     *
     * @param release `uname -r` 或 `/proc/version` 文本；只取 `"Linux version "` 之后、
     *   首个空白之前的 token（ref `uts_release_token` :370-386）。
     * @param preempt 是否写 `preempt`。
     * @param moduleUnload 是否写 `mod_unload`；对应 ref 的 `!facts.module_force_unload`。
     * @param modversions 是否写 `modversions`。
     * @return 与 ref 相同的 required 串；release 不可解析，或结果长度达到
     *   [VERMAGIC_MAX_BYTES]（192）时返回空串 —— 对应 ref `required_vermagic` 返回
     *   false 的两种情形。
     */
    fun required(
        release: String,
        preempt: Boolean,
        moduleUnload: Boolean,
        modversions: Boolean,
    ): String {
        val releaseToken = utsReleaseToken(release)
        if (releaseToken.isEmpty()) {
            return ""
        }
        val tokens = ArrayList<String>(6)
        tokens += releaseToken
        tokens += SMP_TOKEN
        if (preempt) {
            tokens += PREEMPT_TOKEN
        }
        if (moduleUnload) {
            tokens += MODULE_UNLOAD_TOKEN
        }
        if (modversions) {
            tokens += MODVERSIONS_TOKEN
        }
        tokens += ARCH_TOKEN
        val text = tokens.joinToString(" ")
        /* ref 的 char[192] 需要给 NUL 留一位：length + 1 > 192 即失败。 */
        return if (text.toByteArray(Charsets.UTF_8).size >= VERMAGIC_MAX_BYTES) "" else text
    }

    /**
     * vermagic 判型（ref `classify_vermagic_diff` :219-234）。
     *
     * 本 API 没有 ref 的 `has_crcs` 参数：先判等（precheck 的 `None`），再按 ref 的
     * `!has_crcs` 分支比较首个空格之后的尾串 —— 尾串相同 ⇒ [Verdict.RELEASE]，
     * 尾串不同 ⇒ [Verdict.OPTIONS]；任一输入为空 ⇒ [Verdict.UNPARSABLE]。
     * 带 CRC 的模块在 precheck 里“尾串相同即通过”，因此走到判型的只剩 Options；
     * 这里保留整串语义，便于把 release-only 差异单独诊断出来。
     *
     * @param current 模块 `.modinfo` 里的 `vermagic=` 值。
     * @param required [required] 构造的目标串。
     */
    fun classify(current: String, required: String): Verdict {
        if (current.isEmpty() || required.isEmpty()) {
            return Verdict.UNPARSABLE
        }
        if (current == required) {
            return Verdict.NONE
        }
        return if (afterFirstSpace(current) == afterFirstSpace(required)) {
            Verdict.RELEASE
        } else {
            Verdict.OPTIONS
        }
    }

    /**
     * 原地重写模块镜像里的 `vermagic=` 条目（ref `rewrite_vermagic` :574-621）。
     *
     * 事务性：解析、定位、容量判断全部完成后才复制并写入；任何失败都直接返回，
     * 入参 [image] 一个字节都不改。成功后把旧条目槽里新值之后的余量全部清零。
     *
     * @param image 完整模块镜像（只读）。
     * @param required 目标 vermagic 串，通常来自 [required]；为空时返回
     *   [RewriteResult.MissingModinfo]（ref 的 `VermagicMissing`）。
     * @return [RewriteResult.Rewritten] / [RewriteResult.SlotTooSmall] /
     *   [RewriteResult.MissingModinfo] / [RewriteResult.BadElf]。
     */
    fun rewrite(image: ByteArray, required: String): RewriteResult {
        if (!ElfModinfo43284.envelopeOk(image)) {
            return RewriteResult.BadElf
        }
        val section = when (val found = ElfModinfo43284.findSection(image, ElfModinfo43284.MODINFO)) {
            is ElfModinfo43284.Lookup.Found -> found
            ElfModinfo43284.Lookup.Absent -> return RewriteResult.MissingModinfo
            ElfModinfo43284.Lookup.Malformed -> return RewriteResult.BadElf
        }
        val entry = ElfModinfo43284.entries(image, section.offset, section.size)
            .firstOrNull { it.text.startsWith(ElfModinfo43284.VERMAGIC_PREFIX) }
            ?: return RewriteResult.MissingModinfo
        if (required.isEmpty()) {
            /* ref :575-578 的 VermagicMissing；没有目标值就等同于没有可写载体。 */
            return RewriteResult.MissingModinfo
        }
        val prefix = ElfModinfo43284.VERMAGIC_PREFIX.toByteArray(Charsets.US_ASCII)
        val value = required.toByteArray(Charsets.UTF_8)
        /* 槽从条目起点计，前缀也在槽内；完整判据是 prefix + value + NUL <= slot。
         * ref :599-603 只比 value.size()+1，短 8 B；这里按完整条目判，避免越过本条目
         * 的 NUL 写进下一个条目。 */
        val need = prefix.size.toLong() + value.size.toLong() + 1L
        if (entry.start.toLong() + entry.slot.toLong() > section.size.toLong()) {
            return RewriteResult.BadElf
        }
        if (need > entry.slot.toLong()) {
            return RewriteResult.SlotTooSmall(
                need = if (need > Int.MAX_VALUE.toLong()) Int.MAX_VALUE else need.toInt(),
                have = entry.slot,
            )
        }
        val rewritten = image.copyOf()
        val base = section.offset + entry.start
        for (index in prefix.indices) {
            rewritten[base + index] = prefix[index]
        }
        for (index in value.indices) {
            rewritten[base + prefix.size + index] = value[index]
        }
        for (index in (prefix.size + value.size) until entry.slot) {
            rewritten[base + index] = 0.toByte()
        }
        return RewriteResult.Rewritten(rewritten)
    }

    /** ref `char required_buf[kVermagicMaxBytes]`（lkm_image.hpp:67）。 */
    private const val VERMAGIC_MAX_BYTES: Int = 192

    private const val SMP_TOKEN: String = "SMP"
    private const val PREEMPT_TOKEN: String = "preempt"
    private const val MODULE_UNLOAD_TOKEN: String = "mod_unload"
    private const val MODVERSIONS_TOKEN: String = "modversions"
    private const val ARCH_TOKEN: String = "aarch64"
    private const val LINUX_VERSION_PREFIX: String = "Linux version "

    /** ref `uts_release_token` :370-386：剥前缀、去前导空白、取到下一个空白。 */
    private fun utsReleaseToken(text: String): String {
        val body = if (text.startsWith(LINUX_VERSION_PREFIX)) {
            text.substring(LINUX_VERSION_PREFIX.length)
        } else {
            text
        }
        val begin = body.indexOfFirst { !isVersionSpace(it) }
        if (begin < 0) {
            return ""
        }
        return body.substring(begin).takeWhile { !isVersionSpace(it) }
    }

    private fun isVersionSpace(value: Char): Boolean =
        value == ' ' || value == '\t' || value == '\n' || value == '\r'

    /** ref `after_first_space` :209-215：首个空格之后的全部内容；没有空格则为空串。 */
    private fun afterFirstSpace(text: String): String {
        val space = text.indexOf(' ')
        return if (space < 0) "" else text.substring(space + 1)
    }
}

/**
 * 两个 object 共用的只读 ELF64 / `.modinfo` 解析（无状态）。
 *
 * 参照 `ghostlock-app-ref@5f184760` 的
 * `src/core/backend/cve_2026_43284/lkm/lkm_image.cpp` 同匿名命名空间辅助函数：
 *  - `validate_elf_envelope` :145-170；
 *  - `find_section` :90-143；
 *  - `next_modinfo_entry` :184-199。
 *
 * 只做边界检查与字节视图，不做任何写入；写路径唯一入口是
 * [ModuleVermagic43284.rewrite]。
 */
internal object ElfModinfo43284 {
    const val MODINFO: String = ".modinfo"
    const val NAME_PREFIX: String = "name="
    const val VERMAGIC_PREFIX: String = "vermagic="

    private const val ELF_HEADER_BYTES: Int = 64
    private const val SECTION_HEADER_BYTES: Int = 64
    private const val ET_REL: Int = 1
    private const val EM_AARCH64: Int = 0xB7
    private const val MAX_MODULE_BYTES: Int = 64 * 1024 * 1024

    /** `.modinfo` 段查找结果；[Malformed] 与“没有这个段”分开。 */
    sealed interface Lookup {
        data class Found(val offset: Int, val size: Int) : Lookup
        data object Absent : Lookup
        data object Malformed : Lookup
    }

    /**
     * 一个 `.modinfo` 条目。
     *
     * @param start 条目在段内的起始偏移。
     * @param text 条目文本（到首个 NUL 为止；未终止的末条目到段尾）。
     * @param slot 条目槽字节：文本长度 + 1（含终止 NUL）；未终止的末条目为
     *   “段尾 - 起点 + 1”，与 ref :175-182 的注释一致。
     */
    data class Entry(val start: Int, val text: String, val slot: Int)

    /** ref `validate_elf_envelope` :145-170：ELF64、小端、ET_REL、AArch64。 */
    fun envelopeOk(image: ByteArray): Boolean {
        if (image.size < ELF_HEADER_BYTES || image.size > MAX_MODULE_BYTES) {
            return false
        }
        if (image[0] != 0x7F.toByte() ||
            image[1] != 'E'.code.toByte() ||
            image[2] != 'L'.code.toByte() ||
            image[3] != 'F'.code.toByte()
        ) {
            return false
        }
        if (image[4] != 2.toByte() || image[5] != 1.toByte()) {
            return false
        }
        if (u16(image, 0x10) != ET_REL || u16(image, 0x12) != EM_AARCH64) {
            return false
        }
        return true
    }

    /** ref `find_section` :90-143：按 `.shstrtab` 解析段名，返回目标段的范围。 */
    fun findSection(image: ByteArray, name: String): Lookup {
        if (image.size < ELF_HEADER_BYTES) {
            return Lookup.Malformed
        }
        val fileSize = image.size.toLong()
        val shoff = u64(image, 0x28)
        val shentsize = u16(image, 0x3A)
        val shnum = u16(image, 0x3C)
        val shstrndx = u16(image, 0x3E)
        if (shentsize != SECTION_HEADER_BYTES || shnum == 0 || shstrndx >= shnum) {
            return Lookup.Malformed
        }
        if (shoff < 0L || shoff > fileSize) {
            return Lookup.Malformed
        }
        if (shnum.toLong() * SECTION_HEADER_BYTES.toLong() > fileSize - shoff) {
            return Lookup.Malformed
        }
        val shstrHeader = shoff + shstrndx.toLong() * SECTION_HEADER_BYTES.toLong()
        val strOff = u64(image, shstrHeader.toInt() + 0x18)
        val strSize = u64(image, shstrHeader.toInt() + 0x20)
        if (strOff < 0L || strSize < 0L || strOff > fileSize || strSize > fileSize - strOff) {
            return Lookup.Malformed
        }
        val strBegin = strOff.toInt()
        val strEnd = (strOff + strSize).toInt()
        for (index in 0 until shnum) {
            val header = (shoff + index.toLong() * SECTION_HEADER_BYTES.toLong()).toInt()
            val nameOff = u32(image, header)
            if (nameOff >= strSize) {
                continue
            }
            val nameStart = strBegin + nameOff.toInt()
            if (nameStart < strBegin || nameStart >= strEnd) {
                continue
            }
            if (cstring(image, nameStart, strEnd) != name) {
                continue
            }
            val sectionOff = u64(image, header + 0x18)
            val sectionSize = u64(image, header + 0x20)
            if (sectionOff < 0L || sectionSize < 0L ||
                sectionOff > fileSize || sectionSize > fileSize - sectionOff
            ) {
                return Lookup.Malformed
            }
            return Lookup.Found(sectionOff.toInt(), sectionSize.toInt())
        }
        return Lookup.Absent
    }

    /** ref `next_modinfo_entry` :184-199：按 NUL 切条目，未终止的末条目也返回一次。 */
    fun entries(image: ByteArray, offset: Int, size: Int): List<Entry> {
        val result = ArrayList<Entry>()
        var pos = 0
        while (pos < size) {
            val start = pos
            var end = start
            while (end < size && image[offset + end] != 0.toByte()) {
                end += 1
            }
            result += Entry(
                start = start,
                text = String(image, offset + start, end - start, Charsets.ISO_8859_1),
                slot = end - start + 1,
            )
            if (end == size) {
                break
            }
            pos = end + 1
        }
        return result
    }

    private fun cstring(image: ByteArray, begin: Int, end: Int): String {
        var stop = begin
        while (stop < end && image[stop] != 0.toByte()) {
            stop += 1
        }
        return String(image, begin, stop - begin, Charsets.ISO_8859_1)
    }

    private fun u16(image: ByteArray, at: Int): Int =
        (image[at].toInt() and 0xFF) or ((image[at + 1].toInt() and 0xFF) shl 8)

    private fun u32(image: ByteArray, at: Int): Long =
        (image[at].toLong() and 0xFFL) or
            ((image[at + 1].toLong() and 0xFFL) shl 8) or
            ((image[at + 2].toLong() and 0xFFL) shl 16) or
            ((image[at + 3].toLong() and 0xFFL) shl 24)

    private fun u64(image: ByteArray, at: Int): Long {
        var value = 0L
        for (index in 0 until 8) {
            value = value or ((image[at + index].toLong() and 0xFFL) shl (8 * index))
        }
        return value
    }
}
