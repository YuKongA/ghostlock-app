package com.ghostlock.app.backend.core

import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Test

/** The route is resolved before spawn and has exactly one launcher owner. */
class ExecutableProcessRoutingTest {
    @Test
    fun usesShizukuOnlyWhenBinderAndPermissionAreBothPresent() {
        assertEquals(
            ExecutableProcessRoute.SHIZUKU,
            ExecutableProcessRouting.select(binderReachable = true, permissionGranted = true),
        )
        assertEquals(
            ExecutableProcessRoute.APP,
            ExecutableProcessRouting.select(binderReachable = false, permissionGranted = true),
        )
        assertEquals(
            ExecutableProcessRoute.APP,
            ExecutableProcessRouting.select(binderReachable = true, permissionGranted = false),
        )
        assertEquals(
            ExecutableProcessRoute.APP,
            ExecutableProcessRouting.select(binderReachable = false, permissionGranted = false),
        )
    }

    /** Confirms one selected launcher owns each run, including an uncertain Shizuku failure. */
    @Test
    fun selectedLauncherRunsAloneAndShizukuFailureDoesNotFallback() = runBlocking {
        var appStarts = 0
        var shizukuStarts = 0

        val appResult = ExecutableProcessRouting.dispatch(
            route = ExecutableProcessRoute.APP,
            app = { appStarts += 1; "app-result" },
            shizuku = { shizukuStarts += 1; "shizuku-result" },
        )
        assertEquals("app-result", appResult)
        assertEquals(1, appStarts)
        assertEquals(0, shizukuStarts)

        val failure = runCatching {
            ExecutableProcessRouting.dispatch(
                route = ExecutableProcessRoute.SHIZUKU,
                app = { appStarts += 1; "app-fallback" },
                shizuku = {
                    shizukuStarts += 1
                    throw IllegalStateException("remote-start-uncertain")
                },
            )
        }.exceptionOrNull()
        assertEquals("remote-start-uncertain", failure?.message)
        assertEquals(1, appStarts)
        assertEquals(1, shizukuStarts)
    }

    @Test
    fun cve43284RuntimeArgumentsAreGeneratedBySelectedLauncherNotCallerInput() {
        assertEquals(
            null,
            ExecutableBackendProcess.validateRequest(
                backendKey = "43284Glued",
                binaryName = "lib43284Glued.so",
                arguments = emptyList(),
                stdin = null,
                files = emptyMap(),
            ),
        )
        assertEquals(
            "exec:43284:caller-arguments-forbidden",
            ExecutableBackendProcess.validateRequest(
                backendKey = "43284Glued",
                binaryName = "lib43284Glued.so",
                arguments = listOf("--spi", "1"),
                stdin = null,
                files = emptyMap(),
            ),
        )
    }
}
