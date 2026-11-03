package com.ghostlock.app.backend.core

import android.content.pm.PackageManager
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.backend.cve43499glued.Glued43499ProfileCodec
import com.ghostlock.app.backend.declarations.BackendSpec
import com.ghostlock.app.backend.declarations.cve43284glued.Backend43284Glued
import com.ghostlock.app.backend.declarations.cve43499glued.Backend43499Glued
import com.ghostlock.app.core.flow.FlowResult
import com.ghostlock.app.shizuku.ShizukuBackendClient
import com.ghostlock.app.shizuku.host.ShizukuBackendHosts
import com.ghostlock.app.ui.backend.BackendConfigStore
import com.ghostlock.app.ui.backend.BackendRunDispatch
import com.ghostlock.app.ui.backend.BackendRunDispatcher
import com.ghostlock.app.ui.backend.BackendRunGate
import com.ghostlock.app.ui.backend.BackendRunUiState
import com.ghostlock.app.ui.backend.RunPageContext
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import rikka.shizuku.Shizuku

/**
 * Exercises each production Glued Flow against an APK-local, no-op ELF fixture.
 *
 * @note Tests skip unless the packaged executables contain the fixture marker; production payloads are never run.
 */
@RunWith(AndroidJUnit4::class)
class GluedExecFixtureSmokeTest {
    @Test
    fun dispatches43499FlowThroughTheSelectedLauncher() = runBlocking {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val profileStore = BackendConfigStore.open(
            context = context,
            cve = Backend43499Glued.cve,
            schema = Backend43499Glued.configSchema,
        )
        val previousReference = profileStore.getString("profile_reference")
        try {
            val profile = Glued43499ProfileCodec.builtins(context).firstOrNull { asset ->
                runCatching { Glued43499ProfileCodec.resolve(context, "builtin:${asset.release}") }.isSuccess
            } ?: error("fixture-smoke:no-valid-builtin-profile")
            val reference = "builtin:${profile.release}"
            val inputBytes = Glued43499ProfileCodec.resolve(context, reference).binary.size
            Backend43499Glued.selectProfile(reference, profileStore)
            assertFixtureFlow(
                spec = Backend43499Glued,
                backendKey = "43499-Glued",
                binaryName = "lib43499-Glued.so",
                expectedInputBytes = inputBytes,
            )
        } finally {
            profileStore.putString("profile_reference", previousReference)
            Backend43499Glued.restoreConfig(profileStore)
        }
    }

    @Test
    fun dispatches43284FlowThroughTheSelectedLauncherAndClosesRuntime() = runBlocking {
        assertFixtureFlow(
            spec = Backend43284Glued,
            backendKey = "43284Glued",
            binaryName = "lib43284Glued.so",
            expectedInputBytes = 0,
        )
    }

    /** Runs one selectable backend through its real dispatcher, Flow, and exec launcher. */
    private suspend fun assertFixtureFlow(
        spec: BackendSpec,
        backendKey: String,
        binaryName: String,
        expectedInputBytes: Int,
    ) {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        val fixture = context.applicationInfo.nativeLibraryDir + "/" + binaryName
        val marker = "fixture=ghostlock-exec-noop-v1".toByteArray(Charsets.UTF_8)
        val backendMarker = "backend=$backendKey".toByteArray(Charsets.UTF_8)
        val fixtureFile = java.io.File(fixture)
        val executableBytes = fixtureFile.takeIf { it.isFile }?.readBytes() ?: ByteArray(0)
        Log.i(
            "GluedExecFixtureSmoke",
            "fixture-check backend=$backendKey path=$fixture exists=${fixtureFile.isFile} " +
                "can-execute=${fixtureFile.canExecute()} bytes=${executableBytes.size}",
        )
        assumeTrue("No-op fixture binary is not installed", executableBytes.contains(marker))
        assumeTrue("No-op fixture identity does not match", executableBytes.contains(backendMarker))

        val binderReachable = runCatching { Shizuku.pingBinder() }.getOrDefault(false)
        val permissionGranted = binderReachable && runCatching {
            Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED
        }.getOrDefault(false)
        val route = ExecutableProcessRouting.select(binderReachable, permissionGranted)
        InstrumentationRegistry.getArguments().getString("expected_route")?.let { expected ->
            assertEquals("route selection", expected, route.name)
        }

        if (route == ExecutableProcessRoute.SHIZUKU) {
            val backendId = when (backendKey) {
                "43499-Glued" -> ShizukuBackendHosts.CVE_43499_GLUED
                "43284Glued" -> ShizukuBackendHosts.CVE_43284_GLUED
                else -> error("fixture-smoke:unsupported-backend")
            }
            assertTrue(
                "Shizuku preflight must be ready before dispatch",
                ShizukuBackendClient(context, backendId).availabilityReasons().isEmpty(),
            )
        }

        val decision = BackendRunGate.decide(
            spec = spec,
            readiness = spec.readiness(),
            context = RunPageContext(),
        )
        assertTrue("backend gate rejected $backendKey: ${decision.reasons}", decision.allowed)

        val output = mutableListOf<String>()
        val dispatch = BackendRunDispatcher.dispatch(spec, decision, context) { line -> output += line }
        assertTrue("dispatcher did not execute the Flow", dispatch is BackendRunDispatch.Executed)
        val state = (dispatch as BackendRunDispatch.Executed).state
        assertTrue("Flow did not return Finished: $state", state is BackendRunUiState.Finished)
        val result = (state as BackendRunUiState.Finished).result
        assertTrue("fixture did not exit successfully: $result", result is FlowResult.Ok)
        assertTrue("fixture output missing: $output", output.contains("fixture=ghostlock-exec-noop-v1"))
        assertTrue("fixture backend output missing: $output", output.contains("backend=$backendKey"))
        assertTrue("fixture stdin byte count mismatch: $output", output.contains("stdin_bytes=$expectedInputBytes"))
        Log.i(
            "GluedExecFixtureSmoke",
            "backend=$backendKey route=$route terminal=exit-0 fixture=true output=${output.joinToString("|")}",
        )
    }

    /** Returns true when a fixture marker is present without decoding unrelated executable bytes. */
    private fun ByteArray.contains(needle: ByteArray): Boolean =
        needle.isNotEmpty() && indices.any { start ->
            start + needle.size <= size && needle.indices.all { offset -> this[start + offset] == needle[offset] }
        }
}
