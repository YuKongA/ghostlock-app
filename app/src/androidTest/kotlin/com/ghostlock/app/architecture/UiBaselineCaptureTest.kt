package com.ghostlock.app.architecture

import android.graphics.Bitmap
import android.view.Surface
import androidx.activity.ComponentActivity
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.test.isRoot
import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.printToString
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.io.FileOutputStream
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Captures approved production-composable D0 baselines and performs the non-interactive M5 walk.
 *
 * @note The single test method is selected by the `suite` instrumentation argument; no branch clicks Run.
 */
@RunWith(AndroidJUnit4::class)
class UiBaselineCaptureTest {
    @get:Rule
    val composeRule = createAndroidComposeRule<ComponentActivity>()

    private val currentCase = mutableStateOf<D0SurfaceCase?>(null)

    /**
     * Runs one narrow, wide, or M5 device-walk transaction.
     *
     * @return Unit after all requested surfaces are rendered and verified.
     * @throws IllegalArgumentException When the caller supplies an unknown suite.
     * @note Screenshots and semantics are written only for narrow and wide D0 capture suites.
     */
    @Test
    fun captureApprovedProductionSurfaces() {
        val suite = InstrumentationRegistry.getArguments().getString(ARG_SUITE)
            ?: throw IllegalArgumentException("missing instrumentation argument: $ARG_SUITE")
        composeRule.setContent {
            val fixture = currentCase.value
            if (fixture != null) {
                UiBaselineFixtures.Render(fixture)
            }
        }

        when (suite) {
            SUITE_NARROW, SUITE_WIDE -> captureSuite(suite)
            SUITE_M5_DEVICE_WALK -> runM5DeviceWalk()
            else -> throw IllegalArgumentException("unsupported D0 suite: $suite")
        }
    }

    /**
     * Captures every active member for one approved display layout.
     *
     * @param suite Narrow or wide matrix selector.
     * @return Unit after PNG and JSON files are durably replaced in app-private storage.
     * @throws AssertionError When locale, rotation, bounds, counts, or semantics tags differ.
     * @note The narrow transaction clears prior output; the following wide transaction appends M3.
     */
    private fun captureSuite(suite: String) {
        assertActiveEnvironment(suite)
        val cases = UiBaselineFixtures.activeCases(suite)
        assertEquals(if (suite == SUITE_NARROW) 52 else 16, cases.size)

        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val outputRoot = File(instrumentation.targetContext.filesDir, OUTPUT_DIRECTORY)
        if (suite == SUITE_NARROW) {
            assertTrue("failed to clear previous D0 capture", !outputRoot.exists() || outputRoot.deleteRecursively())
        }

        var semanticsRecords = 0
        for (fixture in cases) {
            show(fixture)
            val semanticsTree = composeRule
                .onAllNodes(isRoot(), useUnmergedTree = true)
                .printToString(maxDepth = 100)
            assertTrue("empty semantics tree for ${fixture.memberId}", semanticsTree.isNotBlank())
            assertGalleryPanels(fixture)

            val groupDirectory = File(outputRoot, fixture.group)
            val screenshot = File(groupDirectory, "${fixture.memberId}.png")
            val semantics = File(groupDirectory, "${fixture.memberId}.json")
            val bitmap = requireNotNull(instrumentation.uiAutomation.takeScreenshot()) {
                "UiAutomation returned no screenshot for ${fixture.memberId}"
            }
            writePngAtomically(screenshot, bitmap)
            bitmap.recycle()
            writeJsonAtomically(semantics, semanticsDocument(fixture, semanticsTree))
            semanticsRecords += fixture.semanticsRecords.size
        }

        println(
            "D0_CAPTURE_FIXTURE=PASS SUITE=$suite " +
                "SCREENSHOTS=${cases.size} SEMANTICS_FILES=${cases.size} " +
                "SEMANTICS_RECORDS=$semanticsRecords",
        )
    }

    /**
     * Renders six menu surfaces and all four standard semantic ConfigCards.
     *
     * @return Unit after all ten production surfaces expose a semantics root.
     * @throws AssertionError When any surface fails to compose or expose the D0 root.
     * @note No click is issued, so the idle Run page cannot dispatch an attack.
     */
    private fun runM5DeviceWalk() {
        val menus = UiBaselineFixtures.deviceWalkMenus()
        val cards = UiBaselineFixtures.deviceWalkConfigCards()
        assertEquals(6, menus.size)
        assertEquals(4, cards.size)
        for (fixture in menus + cards) {
            show(fixture)
            val semanticsTree = composeRule
                .onAllNodes(isRoot(), useUnmergedTree = true)
                .printToString(maxDepth = 100)
            assertTrue("empty M5 semantics for ${fixture.surface}", semanticsTree.isNotBlank())
        }
        println("M5_UI_WALK=PASS MENU=${menus.size} CONFIG_CARDS=${cards.size}")
    }

    /**
     * Replaces the current keyed production surface and waits for UI/Compose idleness.
     *
     * @param fixture Next approved matrix or device-walk member.
     * @return Unit after the D0 root exists.
     * @throws AssertionError When the production surface does not create the tagged root.
     * @note Keyed composition disposes remembered page state between members.
     */
    private fun show(fixture: D0SurfaceCase) {
        composeRule.runOnIdle { currentCase.value = fixture }
        composeRule.waitForIdle()
        InstrumentationRegistry.getInstrumentation().waitForIdleSync()
        composeRule.onNodeWithTag(UiBaselineFixtures.ROOT_TAG, useUnmergedTree = true).assertExists()
    }

    /**
     * Checks the resource locale, display rotation, and exact layout bounds owned by the capture tool.
     *
     * @param suite Narrow or wide active transaction.
     * @return Unit when every instrumentation-visible field matches the D0 contract.
     * @throws AssertionError On a stale locale, rotated display, or incorrect display override.
     * @note A301SO device walk intentionally bypasses this Pixel_5-only gate.
     */
    private fun assertActiveEnvironment(suite: String) {
        val resources = composeRule.activity.resources
        val locale = resources.configuration.locales[0].toLanguageTag()
        assertEquals("zh-CN", locale)
        assertEquals(1.0f, resources.configuration.fontScale)
        assertEquals(Surface.ROTATION_0, composeRule.activity.display?.rotation)

        val bounds = composeRule.activity.windowManager.currentWindowMetrics.bounds
        val expectedWidth = if (suite == SUITE_NARROW) 1080 else 1600
        val expectedHeight = if (suite == SUITE_NARROW) 2400 else 2560
        assertEquals(expectedWidth, bounds.width())
        assertEquals(expectedHeight, bounds.height())
    }

    /**
     * Verifies the three named M5c panels without assuming toolkit-specific child semantics.
     *
     * @param fixture Current matrix member.
     * @return Unit after all declared panel tags exist.
     * @throws AssertionError When an approved gallery panel is absent.
     * @note Non-gallery members declare no panel names and require no panel lookup.
     */
    private fun assertGalleryPanels(fixture: D0SurfaceCase) {
        val tags = mapOf(
            "disabled" to UiBaselineFixtures.DISABLED_PANEL_TAG,
            "empty" to UiBaselineFixtures.EMPTY_PANEL_TAG,
            "oversize" to UiBaselineFixtures.OVERSIZE_PANEL_TAG,
        )
        for (panel in fixture.panelNames) {
            val tag = requireNotNull(tags[panel]) { "unknown D0 panel: $panel" }
            composeRule.onNodeWithTag(tag, useUnmergedTree = true).assertExists()
        }
    }

    /**
     * Builds the fixed `{member_id,records[]}` semantics document.
     *
     * @param fixture Current matrix member and required record names.
     * @param semanticsTree Unmerged production semantics snapshot.
     * @return JSON bytes with three named M5c panel assertions when applicable.
     * @note M1/M2 retain the approved normal/rotation/back record names in one member file.
     */
    private fun semanticsDocument(fixture: D0SurfaceCase, semanticsTree: String): ByteArray {
        val records = JSONArray()
        for (recordName in fixture.semanticsRecords) {
            val record = JSONObject()
                .put("name", recordName)
                .put("tree", semanticsTree)
            if (fixture.panelNames.isNotEmpty()) {
                val panels = JSONArray()
                for (panelName in fixture.panelNames) {
                    panels.put(JSONObject().put("name", panelName).put("present", true))
                }
                record.put("panels", panels)
            }
            records.put(record)
        }
        return JSONObject()
            .put("member_id", fixture.memberId)
            .put("records", records)
            .toString(2)
            .plus('\n')
            .toByteArray(Charsets.UTF_8)
    }

    /**
     * Atomically writes one full-display PNG and synchronizes it before rename.
     *
     * @param destination Final app-private output member.
     * @param bitmap Screenshot returned by UiAutomation.
     * @return Unit after the final path contains the complete PNG.
     * @throws IllegalStateException When compression or atomic replacement fails.
     * @note A sibling temporary file prevents a partial member from being packed.
     */
    private fun writePngAtomically(destination: File, bitmap: Bitmap) {
        destination.parentFile?.let { assertTrue("failed to create ${it.path}", it.isDirectory || it.mkdirs()) }
        val temporary = File(destination.parentFile, "${destination.name}.tmp")
        FileOutputStream(temporary).use { output ->
            check(bitmap.compress(Bitmap.CompressFormat.PNG, 100, output)) {
                "PNG compression failed for ${destination.path}"
            }
            output.fd.sync()
        }
        replace(temporary, destination)
    }

    /**
     * Atomically writes one semantics JSON document and synchronizes it before rename.
     *
     * @param destination Final app-private output member.
     * @param payload UTF-8 JSON bytes.
     * @return Unit after the final path contains the complete document.
     * @throws IllegalStateException When atomic replacement fails.
     * @note A sibling temporary file prevents a partial member from being packed.
     */
    private fun writeJsonAtomically(destination: File, payload: ByteArray) {
        destination.parentFile?.let { assertTrue("failed to create ${it.path}", it.isDirectory || it.mkdirs()) }
        val temporary = File(destination.parentFile, "${destination.name}.tmp")
        FileOutputStream(temporary).use { output ->
            output.write(payload)
            output.fd.sync()
        }
        replace(temporary, destination)
    }

    /**
     * Replaces a final output member after deleting an older same-name member.
     *
     * @param temporary Complete sibling temporary file.
     * @param destination Final output member.
     * @return Unit after `renameTo` succeeds.
     * @throws IllegalStateException When delete or rename fails.
     * @note Both paths share a directory, keeping the rename on one filesystem.
     */
    private fun replace(temporary: File, destination: File) {
        check(!destination.exists() || destination.delete()) { "failed to replace ${destination.path}" }
        check(temporary.renameTo(destination)) { "failed to rename ${temporary.path}" }
    }

    private companion object {
        const val ARG_SUITE = "suite"
        const val SUITE_NARROW = "narrow"
        const val SUITE_WIDE = "wide"
        const val SUITE_M5_DEVICE_WALK = "m5-device-walk"
        const val OUTPUT_DIRECTORY = "d0-capture"
    }
}
