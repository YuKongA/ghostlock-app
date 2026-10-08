package com.ghostlock.app.data

import android.app.Application
import androidx.core.content.edit
import com.ghostlock.app.data.profile.Glkv3Decoder
import com.ghostlock.app.data.profile.Glkv3Document
import com.ghostlock.app.data.profile.Glkv3Value
import com.ghostlock.app.domain.model.CpuPair
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import java.nio.file.Files
import java.security.MessageDigest

/**
 * GLKv3-4 production wire lock: every builtin profile resolves to a canonical
 * GLKv3 document, frozen in `native-doc-golden-v3.sha256`. The GLKv3 safe-mode
 * patch is the only mutation and is checked below.
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class NativeDocumentV3EquivalenceTest {
    private val context: Application = RuntimeEnvironment.getApplication()
    private val pair = CpuPair(primary = 0, consumer = 1)
    private val release = "6.1.145-android14-11-maybe-dirty"

    private fun newController(name: String, root: java.io.File): AndroidProfileConfigController =
        AndroidProfileConfigController(
            context = context,
            filesDir = root,
            userProfiles = UserProfileStore(
                directory = root.resolve("user_profiles"),
                assetLoader = AssetConfigLoader(context),
            ),
            preferences = context.getSharedPreferences(name, 0)
                .also { it.edit().clear().commit() },
        )

    @Test
    fun `builtin v3 native documents match the frozen golden`() = runBlocking {
        val golden = readGolden()
        assertTrue("golden fixture is empty", golden.isNotEmpty())
        assertEquals("v3 golden must cover every exported release", 58, golden.size)

        val root = Files.createTempDirectory("native-doc-v3-equivalence").toFile()
        try {
            val controller = newController("native-doc-v3-equivalence", root)
            /* SELF-DIAGNOSTICS (permanent, in the failure message - never on stdout):
             * collect EVERY drifted release instead of aborting at the first one, so
             * the message carries the DRIFT SET and its SIZE. That is what decides
             * whether a re-freeze is a one-line change or an N-line one, and it
             * distinguishes "a single asset converged" from "the declaration
             * default took effect across the catalogue". */
            val drift = mutableListOf<String>()
            for ((release, expected) in golden) {
                val config = controller.load(release, pair)
                assertTrue("$release did not resolve", config.hasProfile)
                val bytes = controller.nativeDocument(config)
                assertNotNull("$release has no v3 native document", bytes)
                val actual = sha256(bytes!!)
                if (actual != expected) {
                    drift += release + " [golden=" + expected.take(12) +
                        " actual=" + actual.take(12) + "]"
                }
            }
            assertEquals(
                "v3 golden drift: " + drift.size + " of " + golden.size +
                    " releases, drift set=" + drift,
                emptyList<String>(),
                drift,
            )
        } finally {
            root.deleteRecursively()
        }
    }

    @Test
    fun `safe mode patch flips only common safe mode on the v3 wire`() = runBlocking {
        val root = Files.createTempDirectory("native-doc-v3-safemode").toFile()
        try {
            val controller = newController("native-doc-v3-safemode", root)
            val config = controller.load(release, pair)
            val original = requireNotNull(controller.nativeDocument(config))
            val decoded = requireNotNull(Glkv3Decoder.decode(original))
            assertEquals(false, safeModeOf(decoded))

            val patched = requireNotNull(NativeProfileDocument.patchSafeMode(original))
            val decodedPatched = requireNotNull(Glkv3Decoder.decode(patched))
            assertEquals(true, safeModeOf(decodedPatched))
            assertEquals("release", decoded.release, decodedPatched.release)
            assertEquals("backend", decoded.backend, decodedPatched.backend)
            assertEquals("terminal", decoded.terminal, decodedPatched.terminal)
            assertEquals("route", decoded.route, decodedPatched.route)
            assertEquals("section count", decoded.sections.size, decodedPatched.sections.size)
        } finally {
            root.deleteRecursively()
        }
    }

    /* HOCON refactor: safe_mode is a ROOT scalar on the wire (native
     * kRootSection), no longer common.safe_mode. */
    private fun safeModeOf(document: Glkv3Document): Boolean? = document.safeMode

    /* NOTE (M3, 2026-10-06): native-doc-golden-v3.sha256 encodes the QUEUE wire
     * shape (available.<id>{ route, queue }); the token form (backend.<id>.steps)
     * was removed from the assets in M3 and the parser support is deleted in M5.
     * The data file must stay comment-free: readGolden() skips blank lines only. */
    private fun readGolden(): Map<String, String> {
        val text = checkNotNull(
            javaClass.classLoader?.getResourceAsStream("native-doc-golden-v3.sha256"),
        ).bufferedReader().use { it.readText() }
        return text.lineSequence()
            .filter { it.isNotBlank() }
            .associate { line ->
                val parts = line.trim().split(' ')
                parts[0] to parts[1]
            }
    }

    private fun sha256(bytes: ByteArray): String =
        MessageDigest.getInstance("SHA-256").digest(bytes)
            .joinToString("") { "%02x".format(it) }
}
