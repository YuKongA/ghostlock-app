package com.ghostlock.app.data.run

import android.content.pm.ApplicationInfo
import android.content.pm.PackageInfo
import com.ghostlock.app.domain.payload.PayloadDefinition
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows

/** Package-priority, mandatory executable, KMI, and launch-only resolver checks. */
@RunWith(RobolectricTestRunner::class)
class RootManagerArtifactResolverTest {
    @Test
    fun freezesInstalledManagerExecutableAndExactKmiArgv() {
        val context = RuntimeEnvironment.getApplication()
        val directory = Files.createTempDirectory("root-manager")
        Files.writeString(directory.resolve("libksud.so"), "manager")
        val packageInfo = PackageInfo().apply {
            packageName = "me.weishu.kernelsu"
            applicationInfo = ApplicationInfo().apply {
                packageName = "me.weishu.kernelsu"
                nativeLibraryDir = directory.toString()
            }
        }
        Shadows.shadowOf(context.packageManager).installPackage(packageInfo)
        val resolver = RootManagerArtifactResolver(context) { "5.15.123-android13-9-gabcdef" }
        val resolved = resolver.resolve(PayloadDefinition(safeMode = true))
        try {
            assertEquals(
                listOf("late-load", "--allow-shell", "--kmi", "android13-5.15", "--safe-mode"),
                resolved.args,
            )
            assertEquals(RootManagerArtifactResolver.ROOT_MANAGER_ARTIFACT_ID, resolved.source.artifact.logicalId)
        } finally {
            resolved.source.master.close()
        }
    }

    @Test
    fun rejectsLaunchOnlyManagerBeforePackageLookup() {
        val context = RuntimeEnvironment.getApplication()
        val resolver = RootManagerArtifactResolver(context) { "5.15.123-android13" }
        assertThrows(IllegalArgumentException::class.java) {
            resolver.resolve(PayloadDefinition(rootManager = "FolkPatch"))
        }
    }
}
