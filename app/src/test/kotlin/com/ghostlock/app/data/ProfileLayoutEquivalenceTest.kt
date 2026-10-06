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
 * value. The legacy fixtures in profile-legacy/ are the pre-R3 files
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
        javaClass.classLoader?.getResourceAsStream("profile-legacy/$name"),
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
            HoconSupport.parseValue(loader.load("profile/index.conf")).asValueMap(),
        )
        val files = index["profiles"].asValueList().orEmpty()
            .mapNotNull { it.asValueMap()?.get("file") as? String } + fragmentFiles
        assertTrue("index lists no profiles", files.size >= 60)

        /* Explicit THREE buckets (migrated / token form / no selection at all):
         * the shared fragments and index.conf declare no selection, so a two-way
         * split would silently absorb them. Every bucket is listed by name. */
        val migratedFiles = mutableListOf<String>()
        val tokenFormFiles = mutableListOf<String>()
        val noSelectionFiles = mutableListOf<String>()
        for (file in files) {
            /* AssetConfigLoader, not a raw asset read: it resolves HOCON includes
             * (profiles include credential-6x / kernelsnitch-6x). */
            val newText = loader.load("profile/" + file)
            assertTrue("$file: empty asset", newText.isNotBlank())
            val declaresTokenList = StepQueueEquivalence.tokenLists(newText).isNotEmpty()
            /* A file that mentions "available" must use exactly one of the two
             * forms; a file that never declares a selection (the shared fragments
             * and index.conf) legitimately has neither. Falsify by deleting the
             * token list of a token-form asset while keeping the word available. */
            assertTrue(
                "$file: a file with an available block must use one of the two selection forms",
                StepQueueEquivalence.isMigrated(newText) || declaresTokenList ||
                    !newText.contains("available"),
            )
            if (!StepQueueEquivalence.isMigrated(newText) && !declaresTokenList) {
                noSelectionFiles += file
                continue
            }
            if (StepQueueEquivalence.isMigrated(newText)) {
                migratedFiles += file
                /* M3 (Lead ruling 2026-10-06): a migrated asset is judged by E1 =
                 * normalised plan equivalence - expected from the LEGACY fixture
                 * (token form) through the catalogue plus the native stepset table,
                 * actual from this asset's own object form. Two independent sources. */
                val fixture = fixtureText(file)
                /* The legacy fixture carries its selection as the flat
                 * backend.<id>.steps token (its documented form); the migrated
                 * asset carries route + queue. Both sides are read independently. */
                val backend = StepQueueEquivalence.backendsOf(newText).single()
                val token = requireNotNull(StepQueueEquivalence.flatTokenOf(fixture, backend)) {
                    file + ": the legacy fixture declares no token for " + backend
                }
                StepQueueEquivalence.assertPlanEquals(
                    StepQueueEquivalence.tokenPlan(backend, token),
                    StepQueueEquivalence.queuePlan(newText, backend),
                )
            } else {
                tokenFormFiles += file
                val expected = flatten(fixtureText(file))
                val actual = flatten(newText)
                assertEquals("$file: owner-qualified flattening drifted", expected, actual)
            }
        }
        /* Content-derived, never hand-copied: after the batch every asset that
         * declares a selection is migrated and none is left in token form. */
        assertEquals("token form must be empty after the M3 batch", 0, tokenFormFiles.size)
        assertEquals(
            "every asset with a selection must be migrated",
            files.size - noSelectionFiles.size,
            migratedFiles.size,
        )
        assertEquals(
            "the three buckets must cover every iterated asset",
            files.size,
            migratedFiles.size + tokenFormFiles.size + noSelectionFiles.size,
)
        println("m3-buckets migrated=" + migratedFiles + " tokenForm=" + tokenFormFiles)
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
