package com.ghostlock.app.ui.screen.module.backends

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class FolkPatchKoTest {
    @Test
    fun parsesKmiFromUnameRelease() {
        assertEquals(
            "android14-5.15",
            FolkPatchKo.parseKmi("5.15.123-android14-11-g1234567-abcd"),
        )
        assertEquals(
            "android16-6.12",
            FolkPatchKo.parseKmi("6.12.81-android16-6-g4f69fc7b210c-ab16167562"),
        )
    }

    @Test
    fun fallsBackForKernelWithoutAndroidToken() {
        assertEquals("android12-5.10", FolkPatchKo.parseKmi("5.10.252-gki-gabcbe4b2bc10"))
    }

    @Test
    fun rejectsUnknownKernel() {
        assertNull(FolkPatchKo.parseKmi("4.19.157-perf-g1234567"))
    }
}
