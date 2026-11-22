package com.ghostlock.app.ui.backend

import android.content.res.Configuration
import androidx.activity.ComponentActivity
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.R
import com.ghostlock.app.architecture.D0SurfaceCase
import com.ghostlock.app.architecture.UiBaselineFixtures
import com.ghostlock.app.composition.backend.BackendCatalogView
import com.ghostlock.app.composition.backend.PluginObjectRef
import com.ghostlock.app.composition.backend.ResolvedBackendProjection
import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.domain.backend.BackendSelectionPolicy
import com.ghostlock.app.domain.backend.ExecutionDomain
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.ui.UiMode
import com.ghostlock.app.ui.theme.ColorMode
import com.ghostlock.plugin.core.PluginContentSha256
import com.ghostlock.plugin.core.PluginPackageIdentity
import java.util.Locale
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/** On-device checks for catalog-backed presentation and nullable selector state. */
@RunWith(AndroidJUnit4::class)
class BackendUiTest {
    @get:Rule
    val composeRule = createAndroidComposeRule<ComponentActivity>()

    /** Host presentation maps the installed Diagnose member and cannot restore Custom membership. */
    @Test
    fun presentationRegistryOnlyMapsCatalogMembership() {
        val entries = entries()
        assertEquals(listOf("DIAGNOSE"), entries.map { entry -> entry.descriptor.key })
        assertTrue(entries.single().descriptor.selectable)
    }

    /** The installed entry resolves ordinary localized resources and both project icon sides. */
    @Test
    fun localizedIdentityAndDualSkinIconsResolve() {
        val base = InstrumentationRegistry.getInstrumentation().targetContext
        val english = localizedContext(base, Locale.ENGLISH)
        val chinese = localizedContext(base, Locale.SIMPLIFIED_CHINESE)
        entries().forEach { entry ->
            assertTrue(english.getString(entry.ui.displayNameRes).isNotBlank())
            assertTrue(chinese.getString(entry.ui.displayNameRes).isNotBlank())
            assertNotEquals(
                english.getString(entry.ui.categoryLabelRes),
                chinese.getString(entry.ui.categoryLabelRes),
            )
            assertTrue(entry.ui.icon.resolve(UiMode.Material).defaultWidth.value > 0f)
            assertTrue(entry.ui.icon.resolve(UiMode.Miuix).defaultWidth.value > 0f)
        }
    }

    /** Both skins show null selection and catalog failure without selecting the first row. */
    @Test
    fun noSelectionAndCatalogFailureRenderUnderBothSkins() {
        val current = mutableStateOf<D0SurfaceCase?>(null)
        composeRule.setContent {
            current.value?.let { fixture -> UiBaselineFixtures.Render(fixture) }
        }
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val noSelection = context.getString(R.string.backend_run_no_backend)
        val failure = context.getString(
            R.string.backend_catalog_failed,
            "synthetic catalog failure",
        )
        for (mode in listOf(UiMode.Material, UiMode.Miuix)) {
            val ready = fixture("backend", mode)
            composeRule.runOnIdle { current.value = ready }
            composeRule.waitForIdle()
            composeRule.onNodeWithTag(UiBaselineFixtures.ROOT_TAG, useUnmergedTree = true).assertExists()
            composeRule.onNodeWithText(noSelection, useUnmergedTree = true).assertExists()

            val failed = fixture("backend_failed", mode)
            composeRule.runOnIdle { current.value = failed }
            composeRule.waitForIdle()
            composeRule.onNodeWithText(noSelection, useUnmergedTree = true).assertExists()
            composeRule.onNodeWithText(failure, useUnmergedTree = true).assertExists()
        }
    }

    private fun entries(): List<BackendUiEntry> = BackendPresentationRegistry.builtin().entries(
        BackendCatalogView(
            revision = 1,
            projections = listOf(
                ResolvedBackendProjection(
                    descriptor = BackendDescriptor(
                        key = "DIAGNOSE",
                        selectionPolicy = BackendSelectionPolicy.SELECTABLE,
                        abortPolicy = AbortPolicy.BEFORE_START_ONLY,
                        executionDomain = ExecutionDomain.APP_OR_SHIZUKU,
                        runCapability = RunCapability.DIAGNOSTIC_ONLY,
                    ),
                    objectRef = PluginObjectRef(
                        packageIdentity = PluginPackageIdentity(
                            publisherId = "com.ghostlock.firstparty",
                            packageId = "com.ghostlock.diagnose",
                        ),
                        variant = "android-arm64-v8a",
                        versionCode = 1,
                        pluginContentSha256 = PluginContentSha256("0".repeat(64)),
                    ),
                ),
            ),
        ),
    )

    private fun fixture(surface: String, mode: UiMode): D0SurfaceCase = D0SurfaceCase(
        memberId = "bxp3_${surface}_${mode.value}",
        group = "bxp3",
        surface = surface,
        mode = mode,
        theme = ColorMode.LIGHT,
        layout = "device",
        semanticsRecords = listOf("normal"),
    )

    /** Creates an isolated locale context without mutating application resources. */
    private fun localizedContext(context: android.content.Context, locale: Locale): android.content.Context {
        val configuration = Configuration(context.resources.configuration)
        configuration.setLocale(locale)
        return context.createConfigurationContext(configuration)
    }
}
