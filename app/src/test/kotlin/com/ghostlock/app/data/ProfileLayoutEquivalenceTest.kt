package com.ghostlock.app.data

import android.app.Application
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

/**
 * S4 R3 per-file flat-equivalence safety gate.
 *
 * For every bundled profile and shared fragment, flatten(canonicalize(new))
 * must equal flatten(canonicalize(legacy fixture)) key by key and value by
 * value. The legacy fixtures in kernel_profiles-legacy/ are the pre-R3 files
 * (includes expanded), so any alias/value drift in the migration fails here.
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class ProfileLayoutEquivalenceTest {
    private val context: Application = RuntimeEnvironment.getApplication()

    private val fragmentFiles = listOf(
        "credential-6x.conf",
        "kernelsnitch-6x.conf",
        "execution-tuning.conf",
        "execution-select-stack.conf",
        "execution-tcp-zerocopy.conf",
    )

    private fun fixtureText(name: String): String = requireNotNull(
        javaClass.classLoader?.getResourceAsStream("kernel_profiles-legacy/$name"),
    ) { "missing legacy fixture $name" }.bufferedReader().use { it.readText() }

    private fun flatten(text: String): Map<String, Any?> {
        val parsed = requireNotNull(HoconSupport.parseValue(text).asValueMap()) {
            "profile did not parse"
        }
        return ProfileLayout.flatten(ProfileLayout.canonicalize(parsed))
    }

    @Test
    fun everyBundledProfileAndFragmentIsFlatEquivalentToItsLegacyFixture() {
        val loader = AssetConfigLoader(context)
        val index = requireNotNull(
            HoconSupport.parseValue(loader.load("kernel_profiles/index.conf")).asValueMap(),
        )
        val files = index["profiles"].asValueList().orEmpty()
            .mapNotNull { it.asValueMap()?.get("file") as? String } + fragmentFiles
        assertTrue("index lists no profiles", files.size >= 60)

        for (file in files) {
            val newText = loader.load("kernel_profiles/$file")
            assertTrue("$file: empty asset", newText.isNotBlank())
            val expected = flatten(fixtureText(file))
            val actual = flatten(newText)
            assertEquals("$file: owner-qualified flattening drifted", expected, actual)
            println("profile-layout-equivalence: $file OK")
        }
    }

    @Test
    fun aliasExceptionsAreRegisteredInTheFlatEquivalence() {
        val flat = flatten(fixtureText("6.1.115-android14-11-ga2521ca27699-ab13294383.conf"))
        /* S4 R6b: selection.steps is cancelled; the legacy backend.steps step
         * id migrates to the combination token at backend.<id>.steps. */
        assertNull(flat["selection.steps"])
        assertEquals("tcp_rootchild", flat["backend.cve_2026_43499.steps"])
        /* kernelsnitch.collisions -> backend.<id>.kernel.kernelsnitch_collisions */
        assertEquals(4L, flat["backend.cve_2026_43499.kernel.kernelsnitch_collisions"])
        /* route.<kind>.compact_waiter -> the shared kernel flag */
        assertEquals(true, flat["backend.cve_2026_43499.kernel.compact_waiter"])
        /* R6a: fallback.to / fallback.route.* are recognized and ignored, so
         * the legacy fallback geometry never reaches the canonical map and the
         * migrated asset no longer carries that route branch. */
        assertNull(flat["common.fallback_route"])
        assertNull(flat["backend.cve_2026_43499.route.select_stack.waiter_shift"])
    }
}
