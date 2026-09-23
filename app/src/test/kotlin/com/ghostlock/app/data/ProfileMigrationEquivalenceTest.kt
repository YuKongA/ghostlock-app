package com.ghostlock.app.data

import android.app.Application
import com.ghostlock.app.domain.model.CpuPair
import com.ghostlock.app.domain.repository.ProfileConfigController
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import java.io.File
import java.nio.file.Files

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class ProfileMigrationEquivalenceTest {
    private val context: Application = RuntimeEnvironment.getApplication()

    @Test
    fun `all remote main 6x profiles resolve identically through legacy import`() = runBlocking {
        val legacyJson = checkNotNull(
            javaClass.classLoader?.getResourceAsStream("remote-main-6x-offsets.json"),
        ).bufferedReader().use { it.readText() }
        val legacyEntries = HoconSupport.parseValue(legacyJson).asValueList()
            ?: error("legacy fixture is not a list")
        val legacyReleases = legacyEntries.mapNotNull {
            it.asValueMap()?.get("release") as? String
        }.toSortedSet()

        val root = Files.createTempDirectory("profile-migration-equivalence").toFile()
        try {
            val baseline = controller(root.resolve("baseline"), offsets = null, preferencesName = "baseline")
            val imported = controller(root.resolve("imported"), legacyJson, preferencesName = "imported")
            val current6x = baseline.builtinReleases()
                .filter { release ->
                    !release.endsWith(ProfileConfigController.TemplateSuffix) &&
                        (release.startsWith("6.1.") || release.startsWith("6.6.") ||
                            release.startsWith("6.12."))
                }
                .toSortedSet()

            assertEquals("fixture must cover every current 6.x builtin", current6x, legacyReleases)
            assertEquals("remote/main fixture size", 50, legacyReleases.size)

            val pair = CpuPair(primary = 0, consumer = 1)
            for (release in legacyReleases) {
                /* Imported documents only apply once loaded. */
                imported.selectUserProfile("remote-main-6x-offsets.json", release, pair)
                val baselineModel = baseline.load(release, pair)
                val baselineNative = baseline.nativeDocument(baselineModel)
                val importedModel = imported.load(release, pair)
                val importedNative = imported.nativeDocument(importedModel)

                assertEquals("resolved ProfileConfig differs for $release", baselineModel, importedModel)
                assertNotNull("baseline native document missing for $release", baselineNative)
                assertNotNull("imported native document missing for $release", importedNative)
                assertArrayEquals(
                    "native profile differs for $release",
                    baselineNative,
                    importedNative,
                )
            }
        } finally {
            root.deleteRecursively()
        }
    }

    private fun controller(
        directory: File,
        offsets: String?,
        preferencesName: String,
    ): AndroidProfileConfigController {
        check(directory.mkdirs()) { "cannot create ${directory.absolutePath}" }
        val store = UserProfileStore(
            directory = directory.resolve("user_profiles"),
            assetLoader = AssetConfigLoader(context),
        )
        if (offsets != null) store.save("remote-main-6x-offsets.json", offsets)
        return AndroidProfileConfigController(
            context = context,
            filesDir = directory,
            userProfiles = store,
            preferences = context.getSharedPreferences(preferencesName, 0).also { it.edit().clear().commit() },
        )
    }
}
