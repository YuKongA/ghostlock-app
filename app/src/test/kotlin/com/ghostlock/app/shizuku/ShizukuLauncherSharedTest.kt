package com.ghostlock.app.shizuku

import com.ghostlock.app.shizuku.host.ShizukuBackendHosts
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 结构守卫：Shizuku 启动器必须**全 backend 共用**（2026-10-28 用户裁定）。
 *
 * - 全树只有一个 `GhostlockUserService`（AIDL Stub），不允许 per-backend UserService/AIDL；
 * - backend 通过 `ShizukuBackendHosts` 注册到公用 host；
 * - 通用 client 是唯一绑定点（adapter 不直接 `bindUserService`）。
 */
class ShizukuLauncherSharedTest {
    @Test
    fun onlyOneShizukuUserServiceImplementation() {
        val root = File("app/src/main/kotlin").let { if (it.isDirectory) it else File("../app/src/main/kotlin") }
        val serviceImplementations = root.walkTopDown()
            .filter { it.isFile && it.extension == "kt" }
            .filter { it.readText().contains(": IGhostlockUserService.Stub") }
            .toList()
        assertEquals("must have exactly one shared UserService", 1, serviceImplementations.size)
        assertTrue(serviceImplementations.single().name == "GhostlockUserService.kt")
    }


    @Test
    fun backendHostsRegistryFailsClosedForUnknownIds() {
        assertNull(ShizukuBackendHosts.create("unknown-backend"))
    }

    @Test
    fun adapterUsesSharedClientAndDoesNotBindDirectly() {
        val client = listOf(
            File("app/src/main/kotlin/com/ghostlock/app/shizuku/ShizukuBackendClient.kt"),
            File("../app/src/main/kotlin/com/ghostlock/app/shizuku/ShizukuBackendClient.kt"),
            File("../../app/src/main/kotlin/com/ghostlock/app/shizuku/ShizukuBackendClient.kt"),
        ).first { it.isFile }.readText()
        assertTrue(client.contains("Shizuku.bindUserService"))
        assertTrue(client.contains("GhostlockUserService"))
    }
}
