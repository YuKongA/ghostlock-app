package com.ghostlock.app.shizuku

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test

/**
 * The staged cve_2026_43284 argv must stay byte-for-byte identical unless the
 * dev-only opt-in is set: the default path is the pre-flag contract.
 */
class Staged43284ArgsTest {

    private val base = listOf(
        "/data/app/lib/arm64/libghostlock.so",
        "--run-cve-2026-43284",
        "/data/local/tmp/ghostlock-app/helper.ko",
        "/data/local/tmp/ghostlock-app/target.bin",
        "--stage=write",
    )

    @Test
    fun `allowDevTarget false keeps the pre-flag argv unchanged`() {
        val argv = staged43284Args(
            binaryPath = "/data/app/lib/arm64/libghostlock.so",
            modulePath = "/data/local/tmp/ghostlock-app/helper.ko",
            targetPath = "/data/local/tmp/ghostlock-app/target.bin",
            stage = "write",
            allowDevTarget = false,
        )
        assertEquals(base, argv)
        assertFalse(argv.contains(ALLOW_DEV_TARGET_FLAG))
    }

    @Test
    fun `allowDevTarget true appends the flag exactly once at the end`() {
        val argv = staged43284Args(
            binaryPath = "/data/app/lib/arm64/libghostlock.so",
            modulePath = "/data/local/tmp/ghostlock-app/helper.ko",
            targetPath = "/data/local/tmp/ghostlock-app/target.bin",
            stage = "write",
            allowDevTarget = true,
        )
        assertEquals(base + ALLOW_DEV_TARGET_FLAG, argv)
        assertEquals(1, argv.count { it == ALLOW_DEV_TARGET_FLAG })
        assertEquals(ALLOW_DEV_TARGET_FLAG, argv.last())
    }
}
