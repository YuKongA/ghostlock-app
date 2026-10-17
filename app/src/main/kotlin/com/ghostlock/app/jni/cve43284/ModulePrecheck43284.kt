package com.ghostlock.app.jni.cve43284

/**
 * 加载前预检：ELF 信封、`.modinfo` 段、`name=` / `vermagic=` 条目。
 *
 * 参照 `ghostlock-app-ref@5f184760` 的
 * `src/core/backend/cve_2026_43284/lkm/lkm_image.cpp`：
 *  - `precheck_module_bytes` :474-560：先 `validate_elf_envelope`（:145-170），
 *    再 `find_section(".modinfo")`（:90-143），最后逐条读 `name=` / `vermagic=`
 *    （:495-522，`next_modinfo_entry` :184-199）；
 *  - 条目的值按 NUL 截断，`depends=` 只是普通条目（可以为空），不是本预检的必选项。
 *
 * 本对象只做只读判定：不写镜像、不读环境、不共享可变状态。失败顺序与 ref 一致
 * （信封 → `.modinfo` → `name=` → `vermagic=`），返回具名结果而不是布尔值。
 */
object ModulePrecheck43284 {
    /** 预检的具名结果。 */
    sealed interface Precheck {
        /**
         * 预检通过。
         *
         * @param name `name=` 的值（最后一个 `name=` 条目，与 ref 的覆盖语义一致）。
         * @param vermagic `vermagic=` 的值（最后一个 `vermagic=` 条目）。
         */
        data class Ok(val name: String, val vermagic: String) : Precheck

        /** ELF64/AArch64 信封或段头表不可用（ref `NotElf` / `NotAarch64` 等）。 */
        data object BadElf : Precheck

        /** 没有 `.modinfo` 段（ref `MissingModinfo` :484-488）。 */
        data object MissingModinfo : Precheck

        /** `.modinfo` 中没有非空的 `name=` 条目（ref `MissingName`）。 */
        data object MissingName : Precheck

        /** `.modinfo` 中没有可解析的 `vermagic=` 条目（ref `VermagicMissing`）。 */
        data object MissingVermagic : Precheck
    }

    /**
     * 预检一个模块镜像。
     *
     * @param image 完整模块镜像（只读）。
     * @return [Precheck.Ok] / [Precheck.BadElf] / [Precheck.MissingModinfo] /
     *   [Precheck.MissingName] / [Precheck.MissingVermagic]。
     */
    fun check(image: ByteArray): Precheck {
        if (!ElfModinfo43284.envelopeOk(image)) {
            return Precheck.BadElf
        }
        val section = when (val found = ElfModinfo43284.findSection(image, ElfModinfo43284.MODINFO)) {
            is ElfModinfo43284.Lookup.Found -> found
            ElfModinfo43284.Lookup.Absent -> return Precheck.MissingModinfo
            ElfModinfo43284.Lookup.Malformed -> return Precheck.BadElf
        }
        var name: String? = null
        var vermagic: String? = null
        for (entry in ElfModinfo43284.entries(image, section.offset, section.size)) {
            if (entry.text.startsWith(ElfModinfo43284.NAME_PREFIX)) {
                /* ref :495-498 是“最后一个 name= 覆盖前一个”，空值同样覆盖。 */
                name = entry.text
                    .substring(ElfModinfo43284.NAME_PREFIX.length)
                    .takeIf { it.isNotEmpty() }
            } else if (entry.text.startsWith(ElfModinfo43284.VERMAGIC_PREFIX)) {
                /* ref :499-502 是“最后一个 vermagic= 覆盖前一个”。 */
                vermagic = entry.text.substring(ElfModinfo43284.VERMAGIC_PREFIX.length)
            }
        }
        val parsedName = name ?: return Precheck.MissingName
        if (vermagic.isNullOrEmpty()) {
            return Precheck.MissingVermagic
        }
        return Precheck.Ok(parsedName, vermagic)
    }
}
