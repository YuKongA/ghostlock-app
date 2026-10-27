package com.ghostlock.app.data

import com.ghostlock.app.data.ManagerScanner.Candidate
import org.junit.Assert.assertEquals
import org.junit.Test

class ManagerScannerTest {
    private fun candidate(
        pkg: String,
        hasKsud: Boolean = true,
        builtin: Boolean = pkg in ManagerScanner.BUILTIN,
        label: String = pkg,
    ) = Candidate(pkg, label, hasKsud, builtin)

    @Test
    fun `resolvePackages keeps builtin order when nothing chosen`() {
        assertEquals(ManagerScanner.BUILTIN, ManagerScanner.resolvePackages(null))
        assertEquals(ManagerScanner.BUILTIN, ManagerScanner.resolvePackages(""))
        assertEquals(ManagerScanner.BUILTIN, ManagerScanner.resolvePackages("  "))
    }

    @Test
    fun `resolvePackages keeps explicit builtin selection strict`() {
        assertEquals(
            listOf("me.weishu.kernelsu"),
            ManagerScanner.resolvePackages("me.weishu.kernelsu"),
        )
    }

    @Test
    fun `resolvePackages keeps explicit custom selection strict`() {
        assertEquals(
            listOf("custom.pkg"),
            ManagerScanner.resolvePackages("custom.pkg"),
        )
    }

    @Test
    fun `sort puts compatible builtins first then compatible then label order`() {
        val sorted = listOf(
            candidate("z.app", hasKsud = false, label = "Zeta"),
            candidate("me.weishu.kernelsu"),
            candidate("a.app", hasKsud = false, label = "Alpha"),
            candidate("m.app", hasKsud = true, label = "Mid"),
        ).sortedWith(ManagerScanner.sortComparator())
        assertEquals(
            listOf("me.weishu.kernelsu", "m.app", "a.app", "z.app"),
            sorted.map { it.packageName },
        )
    }
}
