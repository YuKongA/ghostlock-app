package com.ghostlock.app.shizuku

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * R2b: the app-call argv contract. The production form must stay byte-for-byte
 * the historical list; the dev entry may only append the carrier opt-in, so a
 * dev run is the same document and the same pipeline plus one safety flag.
 */
class AppCallArgsTest {

    private val binary = "/data/app/lib/arm64/libghostlock.so"

    @Test
    fun `production argv is the pre-existing list`() {
        assertEquals(
            listOf(binary, "--ghostlock-app-call", "--enable-status-record"),
            appCallArgs(binary),
        )
    }

    @Test
    fun `run control and observability flags keep their order`() {
        assertEquals(
            listOf(
                binary,
                "--ghostlock-app-call",
                "--enable-status-record",
                "--force-attack",
                "--dump-kernel-log",
                "/data/local/tmp/ghostlock-app/logs",
            ),
            appCallArgs(
                binaryPath = binary,
                forceAttack = true,
                debugDir = "/data/local/tmp/ghostlock-app/logs",
            ),
        )
    }

    @Test
    fun `empty debug dir is omitted`() {
        assertEquals(
            listOf(binary, "--ghostlock-app-call", "--enable-status-record"),
            appCallArgs(binaryPath = binary, debugDir = ""),
        )
    }

    @Test
    fun `dev opt-in appends the flag exactly once at the end`() {
        val argv = appCallArgs(binaryPath = binary, allowDevTarget = true)
        assertTrue(argv.last() == "--allow-dev-target")
        assertEquals(1, argv.count { it == "--allow-dev-target" })
        assertEquals(
            listOf(binary, "--ghostlock-app-call", "--enable-status-record", "--allow-dev-target"),
            argv,
        )
        assertFalse(appCallArgs(binary).contains("--allow-dev-target"))
        assertFalse(argv.any { it == "--stage" || it.startsWith("--stage=") })
        assertFalse(argv.any { it == "--run-cve-2026-43284" || it == "--plugin" })
    }
}
