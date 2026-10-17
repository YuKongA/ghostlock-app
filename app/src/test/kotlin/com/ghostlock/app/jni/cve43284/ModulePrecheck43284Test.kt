package com.ghostlock.app.jni.cve43284

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 加载前预检（本批新增，未接线）。
 *
 * 参照 ghostlock-app-ref@5f184760 的
 * src/core/backend/cve_2026_43284/lkm/lkm_image.cpp:474-560：
 * 信封 → `.modinfo` → `name=` → `vermagic=` 的失败顺序与 ref 一致。
 */
class ModulePrecheck43284Test {
    @Test
    fun okCarriesTheNameAndVermagic() {
        val result = ModulePrecheck43284.check(SyntheticElf43284.build())
        assertTrue(
            "合法模块必须 Ok，实际 " + result.javaClass.simpleName,
            result is ModulePrecheck43284.Precheck.Ok,
        )
        result as ModulePrecheck43284.Precheck.Ok
        assertEquals("ghostlock", result.name)
        assertEquals(SyntheticElf43284.DEFAULT_VERMAGIC, result.vermagic)
    }

    @Test
    fun anEmptyDependsEntryIsAccepted() {
        val image = SyntheticElf43284.build(
            SyntheticElf43284.Spec(
                entries = listOf(
                    "license=GPL",
                    "name=ghostlock",
                    "depends=",
                    "vermagic=" + SyntheticElf43284.DEFAULT_VERMAGIC,
                ),
            ),
        )
        val result = ModulePrecheck43284.check(image)
        assertTrue(result is ModulePrecheck43284.Precheck.Ok)
    }

    @Test
    fun aMissingModinfoSectionIsNamed() {
        val image = SyntheticElf43284.build(SyntheticElf43284.Spec(presentModinfo = false))
        assertEquals(
            ModulePrecheck43284.Precheck.MissingModinfo,
            ModulePrecheck43284.check(image),
        )
    }

    @Test
    fun aMissingOrEmptyNameIsNamed() {
        val missing = SyntheticElf43284.build(
            SyntheticElf43284.Spec(
                entries = listOf(
                    "license=GPL",
                    "vermagic=" + SyntheticElf43284.DEFAULT_VERMAGIC,
                ),
            ),
        )
        assertEquals(ModulePrecheck43284.Precheck.MissingName, ModulePrecheck43284.check(missing))

        val empty = SyntheticElf43284.build(
            SyntheticElf43284.Spec(
                entries = listOf(
                    "name=",
                    "vermagic=" + SyntheticElf43284.DEFAULT_VERMAGIC,
                ),
            ),
        )
        assertEquals(ModulePrecheck43284.Precheck.MissingName, ModulePrecheck43284.check(empty))
    }

    @Test
    fun aMissingOrEmptyVermagicIsNamed() {
        val missing = SyntheticElf43284.build(
            SyntheticElf43284.Spec(
                entries = listOf("license=GPL", "name=ghostlock", "depends="),
            ),
        )
        assertEquals(
            ModulePrecheck43284.Precheck.MissingVermagic,
            ModulePrecheck43284.check(missing),
        )

        val empty = SyntheticElf43284.build(
            SyntheticElf43284.Spec(entries = listOf("name=ghostlock", "vermagic=")),
        )
        assertEquals(
            ModulePrecheck43284.Precheck.MissingVermagic,
            ModulePrecheck43284.check(empty),
        )
    }

    @Test
    fun badEnvelopesAreNamed() {
        assertEquals(ModulePrecheck43284.Precheck.BadElf, ModulePrecheck43284.check(ByteArray(8)))
        assertEquals(
            "ELFCLASS32",
            ModulePrecheck43284.Precheck.BadElf,
            ModulePrecheck43284.check(SyntheticElf43284.build(SyntheticElf43284.Spec(elfClass = 1))),
        )
        assertEquals(
            "大端",
            ModulePrecheck43284.Precheck.BadElf,
            ModulePrecheck43284.check(SyntheticElf43284.build(SyntheticElf43284.Spec(elfData = 2))),
        )
        assertEquals(
            "ET_EXEC",
            ModulePrecheck43284.Precheck.BadElf,
            ModulePrecheck43284.check(SyntheticElf43284.build(SyntheticElf43284.Spec(elfType = 2))),
        )
        assertEquals(
            "x86_64",
            ModulePrecheck43284.Precheck.BadElf,
            ModulePrecheck43284.check(
                SyntheticElf43284.build(
                    SyntheticElf43284.Spec(machine = SyntheticElf43284.MACHINE_X86_64),
                ),
            ),
        )
    }

    @Test
    fun aTruncatedSectionTableIsBadElf() {
        val truncated = SyntheticElf43284.build().copyOf(64)
        assertEquals(ModulePrecheck43284.Precheck.BadElf, ModulePrecheck43284.check(truncated))
    }

    @Test
    fun theLastEntriesWinLikeTheReference() {
        val image = SyntheticElf43284.build(
            SyntheticElf43284.Spec(
                entries = listOf(
                    "name=first",
                    "vermagic=old",
                    "name=second",
                    "vermagic=new",
                ),
            ),
        )
        val result = ModulePrecheck43284.check(image)
        assertTrue(result is ModulePrecheck43284.Precheck.Ok)
        result as ModulePrecheck43284.Precheck.Ok
        assertEquals("second", result.name)
        assertEquals("new", result.vermagic)
    }
}
