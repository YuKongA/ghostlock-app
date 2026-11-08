package com.ghostlock.app.ui.backend

import android.content.res.Configuration
import androidx.activity.ComponentActivity
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.R
import com.ghostlock.app.architecture.D0SurfaceCase
import com.ghostlock.app.architecture.UiBaselineFixtures
import com.ghostlock.app.backend.backends.lua.CustomBackendRuntime
import com.ghostlock.app.composition.backend.BackendRegistry
import com.ghostlock.app.ui.UiMode
import com.ghostlock.app.ui.kit.LocalUiKit
import com.ghostlock.app.ui.kit.MaterialKit
import java.util.Locale
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/** On-device identity checks for both named backend UIs and both project icon sides. */
@RunWith(AndroidJUnit4::class)
class BackendUiTest {
    @get:Rule
    val composeRule = createAndroidComposeRule<ComponentActivity>()

    /** Diagnose and disabled Custom are the only visible same-APK UI projections. */
    @Test
    fun namedEntriesPreserveDefinitionOrderAndSelectionPolicy() {
        val entries = BackendRegistry.uiEntries
        assertEquals(listOf("CUSTOM", "DIAGNOSE"), entries.map { entry -> entry.descriptor.key })
        assertFalse(requireNotNull(entries.firstOrNull { it.descriptor.key == "CUSTOM" }).descriptor.selectable)
        assertTrue(requireNotNull(entries.firstOrNull { it.descriptor.key == "DIAGNOSE" }).descriptor.selectable)
    }

    /** Each entry resolves ordinary localized resources and both project-owned renderer icons. */
    @Test
    fun localizedIdentityAndDualSkinIconsResolve() {
        val base = InstrumentationRegistry.getInstrumentation().targetContext
        val english = localizedContext(base, Locale.ENGLISH)
        val chinese = localizedContext(base, Locale.SIMPLIFIED_CHINESE)
        BackendRegistry.uiEntries.forEach { entry ->
            val englishName = english.getString(entry.ui.displayNameRes)
            val chineseName = chinese.getString(entry.ui.displayNameRes)
            val englishCategory = english.getString(entry.ui.categoryLabelRes)
            val chineseCategory = chinese.getString(entry.ui.categoryLabelRes)
            assertTrue(englishName.isNotBlank())
            assertTrue(chineseName.isNotBlank())
            assertNotEquals(englishCategory, chineseCategory)
            assertTrue(entry.ui.icon.resolve(UiMode.Material).defaultWidth.value > 0f)
            assertTrue(entry.ui.icon.resolve(UiMode.Miuix).defaultWidth.value > 0f)
        }
    }

    /** Diagnose, Custom, and the selector page compose under both project skins. */
    @Test
    fun namedBackendSurfacesRenderUnderBothSkins() {
        val current = mutableStateOf<D0SurfaceCase?>(null)
        composeRule.setContent {
            current.value?.let { fixture -> UiBaselineFixtures.Render(fixture) }
        }
        val surfaces = UiBaselineFixtures.activeCases("narrow").filter { fixture ->
            fixture.theme.name == "LIGHT" && fixture.surface in setOf(
                "backend",
                "config_debug",
                "config_43284",
            )
        }
        assertEquals(6, surfaces.size)
        surfaces.forEach { fixture ->
            composeRule.runOnIdle { current.value = fixture }
            composeRule.waitForIdle()
            composeRule.onNodeWithTag(UiBaselineFixtures.ROOT_TAG, useUnmergedTree = true).assertExists()
        }
    }

    /** A known Custom runtime reason resolves the Android locale resource instead of its English fallback. */
    @Test
    fun customDisabledReasonUsesLocalizedUiResource() {
        val localized = mutableStateOf<android.content.Context?>(null)
        composeRule.setContent {
            localized.value?.let { context ->
                CompositionLocalProvider(
                    LocalContext provides context,
                    LocalUiKit provides MaterialKit,
                ) {
                    MaterialTheme {
                        CustomBackendUi.Content(CustomBackendRuntime.availability())
                    }
                }
            }
        }
        val base = InstrumentationRegistry.getInstrumentation().targetContext
        val expected = listOf(
            localizedContext(base, Locale.ENGLISH).getString(R.string.backend_custom_disabled_summary),
            localizedContext(base, Locale.SIMPLIFIED_CHINESE).getString(R.string.backend_custom_disabled_summary),
        )
        assertNotEquals(expected[0], expected[1])
        listOf(Locale.ENGLISH, Locale.SIMPLIFIED_CHINESE).zip(expected).forEach { (locale, text) ->
            composeRule.runOnIdle { localized.value = localizedContext(base, locale) }
            composeRule.waitForIdle()
            composeRule.onNodeWithText(text, useUnmergedTree = true).assertExists()
        }
    }

    /** Creates an isolated locale context without mutating application resources. */
    private fun localizedContext(context: android.content.Context, locale: Locale): android.content.Context {
        val configuration = Configuration(context.resources.configuration)
        configuration.setLocale(locale)
        return context.createConfigurationContext(configuration)
    }
}
