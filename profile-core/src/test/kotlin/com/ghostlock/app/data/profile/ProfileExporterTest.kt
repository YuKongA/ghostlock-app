package com.ghostlock.app.data.profile

import org.junit.Assert.assertThrows
import org.junit.Test
import java.io.File

class ProfileExporterTest {
    private val src = File("/repo/app/src/main/assets/kernel_profiles")
    private val expected = File("/repo/build/kernel-profiles")

    @Test
    fun `accepts the configured export dir`() {
        ProfileExporter.validateOutputDir(src, expected, expected)
    }

    @Test
    fun `rejects an unrelated build dir outside the project`() {
        val outside = File("/private/tmp/build/user-data")
        assertThrows(IllegalArgumentException::class.java) {
            ProfileExporter.validateOutputDir(src, outside, expected)
        }
    }

    @Test
    fun `rejects the source tree`() {
        val inSource = File("/repo/app/src/main/assets")
        assertThrows(IllegalArgumentException::class.java) {
            ProfileExporter.validateOutputDir(src, inSource, inSource)
        }
    }

    @Test
    fun `rejects the profiles dir itself`() {
        assertThrows(IllegalArgumentException::class.java) {
            ProfileExporter.validateOutputDir(src, src, src)
        }
    }
}
