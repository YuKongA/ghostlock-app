package com.ghostlock.app.jni.cve43284

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume
import org.junit.Test
import java.io.ByteArrayOutputStream

/**
 * vermagic 构造 / 判型 / 原地重写（本批新增，未接线）。
 *
 * 参照 ghostlock-app-ref@5f184760 的
 * src/core/backend/cve_2026_43284/lkm/lkm_image.cpp:219-250 / 370-412 / 574-621。
 *
 * 所有 ELF 都是本文件里的 [SyntheticElf43284] 拼出来的：ELF64 头 + `.shstrtab` +
 * 一个 `.modinfo` 段 + 段头表，不依赖构建产物。真机模块用例只在有副本时运行。
 */
class ModuleVermagic43284Test {
    @Test
    fun requiredFollowsTheReferenceTokenOrder() {
        assertEquals(
            "5.15.202-dirty SMP preempt mod_unload modversions aarch64",
            ModuleVermagic43284.required("5.15.202-dirty", true, true, true),
        )
    }

    @Test
    fun requiredHonoursTheFourOptionCombinations() {
        assertEquals(
            "5.15.202-dirty SMP preempt mod_unload modversions aarch64",
            ModuleVermagic43284.required("5.15.202-dirty", true, true, true),
        )
        assertEquals(
            "5.15.202-dirty SMP mod_unload modversions aarch64",
            ModuleVermagic43284.required("5.15.202-dirty", false, true, true),
        )
        assertEquals(
            "5.15.202-dirty SMP preempt aarch64",
            ModuleVermagic43284.required("5.15.202-dirty", true, false, false),
        )
        assertEquals(
            "5.15.202-dirty SMP aarch64",
            ModuleVermagic43284.required("5.15.202-dirty", false, false, false),
        )
    }

    @Test
    fun requiredTakesOnlyTheReleaseTokenFromProcVersion() {
        assertEquals(
            "5.15.189-x SMP preempt modversions aarch64",
            ModuleVermagic43284.required(
                "Linux version 5.15.189-x (build) #1 SMP PREEMPT",
                preempt = true,
                moduleUnload = false,
                modversions = true,
            ),
        )
        assertEquals(
            "5.15.202-dirty SMP preempt mod_unload modversions aarch64",
            ModuleVermagic43284.required("5.15.202-dirty extra tokens", true, true, true),
        )
    }

    @Test
    fun requiredRejectsAnUnparsableRelease() {
        assertEquals("", ModuleVermagic43284.required("", true, true, true))
        assertEquals("", ModuleVermagic43284.required(" \t\n\r ", true, true, true))
    }

    @Test
    fun classifyMatchesTheFourReferenceVerdicts() {
        assertEquals(
            ModuleVermagic43284.Verdict.NONE,
            ModuleVermagic43284.classify(
                "6.1.0 SMP preempt aarch64",
                "6.1.0 SMP preempt aarch64",
            ),
        )
        assertEquals(
            "只有 release token 不同 ⇒ Release",
            ModuleVermagic43284.Verdict.RELEASE,
            ModuleVermagic43284.classify(
                "6.1.0 SMP preempt aarch64",
                "6.1.1 SMP preempt aarch64",
            ),
        )
        assertEquals(
            "选项尾串不同 ⇒ Options",
            ModuleVermagic43284.Verdict.OPTIONS,
            ModuleVermagic43284.classify(
                "6.1.0 SMP preempt aarch64",
                "6.1.0 SMP mod_unload aarch64",
            ),
        )
        assertEquals(
            ModuleVermagic43284.Verdict.UNPARSABLE,
            ModuleVermagic43284.classify("", "6.1.0 SMP aarch64"),
        )
        assertEquals(
            ModuleVermagic43284.Verdict.UNPARSABLE,
            ModuleVermagic43284.classify("6.1.0 SMP aarch64", ""),
        )
        /* ref 的 !has_crcs 分支：两边都没有选项尾串时，不同只可能是 release。 */
        assertEquals(
            ModuleVermagic43284.Verdict.RELEASE,
            ModuleVermagic43284.classify("6.1.0", "6.1.1"),
        )
    }

    @Test
    fun rewriteZeroFillsTheRestOfTheVermagicSlot() {
        val oldValue = SyntheticElf43284.DEFAULT_VERMAGIC
        val newValue = "5.15.202-dirty SMP modversions aarch64"
        val image = SyntheticElf43284.build()
        val before = image.copyOf()

        val result = ModuleVermagic43284.rewrite(image, newValue)
        assertTrue(
            "容量足够时必须返回 Rewritten，实际 " + result.javaClass.simpleName,
            result is ModuleVermagic43284.RewriteResult.Rewritten,
        )
        val rewritten = (result as ModuleVermagic43284.RewriteResult.Rewritten).bytes
        val at = SyntheticElf43284.indexOf(rewritten, "vermagic=")
        assertTrue("找不到 vermagic= 条目", at >= 0)
        assertEquals("vermagic=", text(rewritten, at, "vermagic=".length))
        assertEquals(newValue, text(rewritten, at + "vermagic=".length, newValue.length))

        val slot = "vermagic=".length + oldValue.length + 1
        for (index in at + "vermagic=".length + newValue.length until at + slot) {
            assertTrue("槽内余量必须清零，偏移 " + index, rewritten[index] == 0.toByte())
        }

        assertArrayEquals("成功路径也不得改动入参", before, image)
        assertArrayEquals(
            "槽前字节不变",
            before.copyOfRange(0, at),
            rewritten.copyOfRange(0, at),
        )
        assertArrayEquals(
            "槽后字节不变",
            before.copyOfRange(at + slot, before.size),
            rewritten.copyOfRange(at + slot, rewritten.size),
        )

        val precheck = ModulePrecheck43284.check(rewritten)
        assertTrue(
            "改完必须能通过预检，实际 " + precheck.javaClass.simpleName,
            precheck is ModulePrecheck43284.Precheck.Ok,
        )
        precheck as ModulePrecheck43284.Precheck.Ok
        assertEquals("ghostlock", precheck.name)
        assertEquals(newValue, precheck.vermagic)
    }

    @Test
    fun rewriteRejectsAOneByteShortSlotAndLeavesTheInputAlone() {
        val oldValue = "5.15.202-dirty".padEnd(70, 'x')
        assertEquals(70, oldValue.length)
        val image = SyntheticElf43284.build(
            SyntheticElf43284.Spec(
                entries = listOf(
                    "license=GPL",
                    "name=ghostlock",
                    "vermagic=" + oldValue,
                ),
            ),
        )
        val before = image.copyOf()
        val newValue = "n".repeat(71)

        val result = ModuleVermagic43284.rewrite(image, newValue)
        assertTrue(
            "旧槽 80 B、新条目 81 B，必须 SlotTooSmall，实际 " + result.javaClass.simpleName,
            result is ModuleVermagic43284.RewriteResult.SlotTooSmall,
        )
        val short = result as ModuleVermagic43284.RewriteResult.SlotTooSmall
        assertEquals(81, short.need)
        assertEquals(80, short.have)
        assertArrayEquals("失败路径必须一个字节都不改", before, image)
    }

    @Test
    fun rewriteWithoutModinfoIsNamedAndLeavesTheInputAlone() {
        val image = SyntheticElf43284.build(SyntheticElf43284.Spec(presentModinfo = false))
        val before = image.copyOf()
        assertEquals(
            ModuleVermagic43284.RewriteResult.MissingModinfo,
            ModuleVermagic43284.rewrite(image, "5.15.202-dirty SMP aarch64"),
        )
        assertArrayEquals(before, image)
    }

    @Test
    fun rewriteWithoutVermagicEntryOrWithAnEmptyRequiredIsNamed() {
        val image = SyntheticElf43284.build(
            SyntheticElf43284.Spec(entries = listOf("license=GPL", "name=ghostlock", "depends=")),
        )
        val before = image.copyOf()
        assertEquals(
            ModuleVermagic43284.RewriteResult.MissingModinfo,
            ModuleVermagic43284.rewrite(image, "5.15.202-dirty SMP aarch64"),
        )
        assertArrayEquals(before, image)

        val valid = SyntheticElf43284.build()
        assertEquals(
            ModuleVermagic43284.RewriteResult.MissingModinfo,
            ModuleVermagic43284.rewrite(valid, ""),
        )
    }

    @Test
    fun rewriteRejectsBadElves() {
        assertEquals(
            ModuleVermagic43284.RewriteResult.BadElf,
            ModuleVermagic43284.rewrite(ByteArray(32), "x"),
        )
        assertEquals(
            ModuleVermagic43284.RewriteResult.BadElf,
            ModuleVermagic43284.rewrite(
                SyntheticElf43284.build(
                    SyntheticElf43284.Spec(machine = SyntheticElf43284.MACHINE_X86_64),
                ),
                "x",
            ),
        )
        val truncated = SyntheticElf43284.build().copyOf(64)
        assertEquals(
            ModuleVermagic43284.RewriteResult.BadElf,
            ModuleVermagic43284.rewrite(truncated, "x"),
        )
    }

    @Test
    fun theMeasuredDeviceSlotCannotHoldTheLocalVermagic() {
        /* 本机实测：该模块的 vermagic 条目槽 80 B（vermagic= + 70 B 旧值 + NUL）。 */
        val oldValue = "5.15.202-dirty".padEnd(70, 'x')
        assertEquals(80, "vermagic=".length + oldValue.length + 1)
        val image = SyntheticElf43284.build(
            SyntheticElf43284.Spec(
                entries = listOf("name=ghostlock", "vermagic=" + oldValue),
            ),
        )
        val localRequired = ModuleVermagic43284.required(
            "5.15.202-android13-5.15.202_r00-dirty-0123456789abc",
            preempt = true,
            moduleUnload = true,
            modversions = true,
        )
        assertEquals("本机串 94 B", 94, localRequired.toByteArray(Charsets.UTF_8).size)

        val before = image.copyOf()
        val result = ModuleVermagic43284.rewrite(image, localRequired)
        assertTrue(
            "槽 80 B 放不下 94 B 的值，必须 SlotTooSmall，实际 " + result.javaClass.simpleName,
            result is ModuleVermagic43284.RewriteResult.SlotTooSmall,
        )
        val short = result as ModuleVermagic43284.RewriteResult.SlotTooSmall
        assertEquals("vermagic= + 94 + NUL", 104, short.need)
        assertEquals(80, short.have)
        assertArrayEquals(before, image)
    }

    @Test
    fun theDeviceModuleCopyRejectsTheLocalVermagic() {
        val bytes = SyntheticElf43284.deviceModuleBytes(javaClass) ?: run {
            Assume.assumeTrue(
                "未找到 /data/local/tmp/ghostlock.ko 的副本（资源 /cve43284/ghostlock.ko " +
                    "或 GHOSTLOCK_TEST_MODULE）；跳过真机字节用例",
                false,
            )
            return
        }
        val before = bytes.copyOf()
        val localRequired = ModuleVermagic43284.required(
            "5.15.202-android13-5.15.202_r00-dirty-0123456789abc",
            preempt = true,
            moduleUnload = true,
            modversions = true,
        )
        val result = ModuleVermagic43284.rewrite(bytes, localRequired)
        assertTrue(
            "真机模块（槽 80 B）放不下本机串（94 B），必须 SlotTooSmall，实际 " +
                result.javaClass.simpleName,
            result is ModuleVermagic43284.RewriteResult.SlotTooSmall,
        )
        assertArrayEquals("失败路径不得改动真机字节", before, bytes)
    }

    private fun text(bytes: ByteArray, at: Int, length: Int): String =
        String(bytes, at, length, Charsets.ISO_8859_1)
}

/**
 * 测试用的最小 ELF64：64 B 头 + `.shstrtab` + 一个 `.modinfo` 段 + 3 个段头。
 *
 * 与 ref 的 `build_elf`（src/core/tests/cve_2026_43284_lkm_test.cpp:100-180）同形；
 * 默认段内容含 `license=` / `name=` / `depends=` / `vermagic=` 四个条目。
 */
internal object SyntheticElf43284 {
    const val MACHINE_AARCH64: Int = 0xB7
    const val MACHINE_X86_64: Int = 62
    const val DEFAULT_VERMAGIC: String =
        "5.15.202-dirty SMP preempt mod_unload modversions aarch64"

    private const val SHSTRTAB = ".shstrtab"
    private const val MODINFO = ".modinfo"

    data class Spec(
        val entries: List<String> = listOf(
            "license=GPL",
            "name=ghostlock",
            "depends=",
            "vermagic=" + DEFAULT_VERMAGIC,
        ),
        val presentModinfo: Boolean = true,
        val machine: Int = MACHINE_AARCH64,
        val elfClass: Int = 2,
        val elfData: Int = 1,
        val elfType: Int = 1,
    )

    fun build(spec: Spec = Spec()): ByteArray {
        val shstr = ByteArrayOutputStream().apply {
            write(0)
            write(SHSTRTAB.toByteArray(Charsets.US_ASCII))
            write(0)
            write(MODINFO.toByteArray(Charsets.US_ASCII))
            write(0)
        }.toByteArray()
        val modinfo = if (spec.presentModinfo) modinfoBlob(spec.entries) else ByteArray(0)

        val out = ByteArrayOutputStream()
        out.write(ByteArray(64))
        val shstrOff = out.size()
        out.write(shstr)
        val modinfoOff = out.size()
        out.write(modinfo)
        while (out.size() % 8 != 0) {
            out.write(0)
        }
        val shoff = out.size()
        out.write(ByteArray(3 * 64))
        val image = out.toByteArray()

        image[0] = 0x7F.toByte()
        image[1] = 'E'.code.toByte()
        image[2] = 'L'.code.toByte()
        image[3] = 'F'.code.toByte()
        image[4] = spec.elfClass.toByte()
        image[5] = spec.elfData.toByte()
        image[6] = 1.toByte()
        put16(image, 0x10, spec.elfType)
        put16(image, 0x12, spec.machine)
        put32(image, 0x14, 1)
        put16(image, 0x34, 64)
        put64(image, 0x28, shoff.toLong())
        put16(image, 0x3A, 64)
        put16(image, 0x3C, 3)
        put16(image, 0x3E, 1)

        var header = shoff + 64
        put32(image, header, 1)
        put32(image, header + 4, 3)
        put64(image, header + 0x18, shstrOff.toLong())
        put64(image, header + 0x20, shstr.size.toLong())

        header = shoff + 128
        put32(
            image,
            header,
            if (spec.presentModinfo) 1 + SHSTRTAB.length + 1 else 0,
        )
        put32(image, header + 4, 1)
        put64(image, header + 0x18, modinfoOff.toLong())
        put64(image, header + 0x20, modinfo.size.toLong())
        return image
    }

    fun indexOf(image: ByteArray, text: String): Int {
        val needle = text.toByteArray(Charsets.ISO_8859_1)
        if (needle.isEmpty()) {
            return 0
        }
        for (start in 0..image.size - needle.size) {
            var match = true
            for (offset in needle.indices) {
                if (image[start + offset] != needle[offset]) {
                    match = false
                    break
                }
            }
            if (match) {
                return start
            }
        }
        return -1
    }

    /**
     * 可选的真机字节：优先 classpath 资源 `/cve43284/ghostlock.ko`，其次环境变量
     * `GHOSTLOCK_TEST_MODULE` 指向的文件；都没有则返回 null（调用方用 Assume 跳过）。
     */
    fun deviceModuleBytes(owner: Class<*>): ByteArray? {
        owner.getResourceAsStream("/cve43284/ghostlock.ko")?.use { stream ->
            return stream.readBytes()
        }
        val path = System.getenv("GHOSTLOCK_TEST_MODULE") ?: return null
        val file = java.io.File(path)
        return if (file.isFile) file.readBytes() else null
    }

    private fun modinfoBlob(entries: List<String>): ByteArray {
        val out = ByteArrayOutputStream()
        for (entry in entries) {
            out.write(entry.toByteArray(Charsets.ISO_8859_1))
            out.write(0)
        }
        return out.toByteArray()
    }

    private fun put16(image: ByteArray, at: Int, value: Int) {
        image[at] = (value and 0xFF).toByte()
        image[at + 1] = ((value ushr 8) and 0xFF).toByte()
    }

    private fun put32(image: ByteArray, at: Int, value: Int) {
        for (index in 0 until 4) {
            image[at + index] = ((value ushr (8 * index)) and 0xFF).toByte()
        }
    }

    private fun put64(image: ByteArray, at: Int, value: Long) {
        for (index in 0 until 8) {
            image[at + index] = ((value ushr (8 * index)) and 0xFFL).toByte()
        }
    }
}
