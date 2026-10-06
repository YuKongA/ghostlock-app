package com.ghostlock.app.data

import com.ghostlock.app.data.profile.NativeProfileGlkv3Adapter
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * CROSS-MODULE guard for the manifest width accessor.
 *
 * The v2 conversion lives in the app module and upgrades its width checks to
 * real range validation from `declaredWidth(path)`. Two things must therefore
 * hold, and this test is the machine check for both:
 *
 *  - the accessor stays PUBLIC: if it ever becomes `internal`, this file stops
 *    compiling (a compile-level failure, not a silent default);
 *  - the value is the manifest declaration: a `uint` path reports its bit
 *    width, while `str`/`array` and undeclared paths report null -- never a
 *    guessed default.
 *
 * Expected values are read from `profile-manifest-v3.tsv` rows (native export).
 */
class ManifestWidthCrossModuleTest {
    @Test
    fun `declaredWidth is public and reports the declared bit width`() {
        assertEquals(
            "arm_hold is a uint with a 2-byte width in the manifest",
            2,
            NativeProfileGlkv3Adapter.declaredWidth(
                "backend.cve_2026_43499.route.multicast_waiter.arm_hold",
            ),
        )
        assertEquals(
            "attempts is a uint with a 1-byte width in the manifest",
            1,
            NativeProfileGlkv3Adapter.declaredWidth(
                "backend.cve_2026_43499.route.multicast_waiter.attempts",
            ),
        )
    }

    @Test
    fun `width-less kinds and undeclared paths report null`() {
        /* str and array carry no bit width: the manifest exports "-". */
        assertNull(
            "route is a str",
            NativeProfileGlkv3Adapter.declaredWidth("backend.cve_2026_43499.route"),
        )
        assertNull(
            "queue is an array",
            NativeProfileGlkv3Adapter.declaredWidth("backend.cve_2026_43499.queue"),
        )
        assertNull(
            "an undeclared path has no width (no implicit prefix rule)",
            NativeProfileGlkv3Adapter.declaredWidth("backend.cve_2026_43499.no_such_key"),
        )
    }
}
