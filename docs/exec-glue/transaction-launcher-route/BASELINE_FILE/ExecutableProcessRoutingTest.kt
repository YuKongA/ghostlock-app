package com.ghostlock.app.backend.core

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
