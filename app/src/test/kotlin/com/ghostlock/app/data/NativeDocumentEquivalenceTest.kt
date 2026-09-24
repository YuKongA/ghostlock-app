package com.ghostlock.app.data

import android.app.Application
import androidx.core.content.edit
import com.ghostlock.app.domain.model.CpuPair
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
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
 * Batch 1 wire-equivalence lock: every builtin profile must still produce the
 * exact `NativeProfileDocument.toBinary()` bytes frozen before the refactor
 * (`app/src/test/resources/native-doc-golden.sha256`).
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class NativeDocumentEquivalenceTest {
    private val context: Application = RuntimeEnvironment.getApplication()
    private val pair = CpuPair(primary = 0, consumer = 1)

    @Test
    fun `builtin native documents match the frozen golden`() = runBlocking {
        val golden = readGolden()
        assertTrue("golden fixture is empty", golden.isNotEmpty())

        val root = Files.createTempDirectory("native-doc-equivalence").toFile()
        try {
            val controller = AndroidProfileConfigController(
                context = context,
                filesDir = root,
                userProfiles = UserProfileStore(
                    directory = root.resolve("user_profiles"),
                    assetLoader = AssetConfigLoader(context),
                ),
                preferences = context.getSharedPreferences("native-doc-equivalence", 0)
                    .also { it.edit().clear().commit() },
            )
            for ((release, expected) in golden) {
                val config = controller.load(release, pair)
                assertTrue("$release did not resolve", config.hasProfile)
                val bytes = controller.nativeDocument(config)
                assertNotNull("$release has no native document", bytes)
                assertEquals("$release wire bytes drifted", expected, sha256(bytes!!))
            }
        } finally {
            root.deleteRecursively()
        }
    }

    private fun readGolden(): Map<String, String> {
        val text = checkNotNull(
            javaClass.classLoader?.getResourceAsStream("native-doc-golden.sha256"),
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
