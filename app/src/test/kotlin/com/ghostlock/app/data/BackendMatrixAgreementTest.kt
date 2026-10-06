package com.ghostlock.app.data

import android.app.Application
import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.component.ComponentAvailability
import com.ghostlock.app.data.profile.NativeProfileGlkv3Adapter
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

/**
 * S4 R3 index.conf backend matrix agreement.
 *
 * The index's backends list is derived from the native-exported GLKv3 owner
 * manifest (owner column); availability mirrors the App backend catalog that
 * mirrors the native catalog. A backend added/dropped on either side fails.
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class BackendMatrixAgreementTest {
    private val context: Application = RuntimeEnvironment.getApplication()

    @Test
    fun indexBackendsMatchTheNativeOwnerManifestAndAvailability() {
        val index = requireNotNull(
            HoconSupport.parseValue(
                AssetConfigLoader(context).load("kernel_profiles/index.conf"),
            ).asValueMap(),
        )
        val entries = index["backends"].asValueList().orEmpty().mapNotNull { it.asValueMap() }
        val matrix = entries.mapNotNull { it["id"] as? String }

        val manifestBackends = NativeProfileGlkv3Adapter.declaredOwners().values
            .filter { it.startsWith("cve_") }
            .toSortedSet()
        assertEquals(
            "index backends must equal the manifest backend owners",
            manifestBackends,
            matrix.toSortedSet(),
        )
        assertEquals(matrix.distinct(), matrix)

        for (entry in entries) {
            val id = entry["id"] as String
            val kind = BackendKind.resolve(id)
            assertNotNull("unknown backend id in index: $id", kind)
            assertEquals(
                "availability drifted for $id",
                ComponentAvailability.backendAvailable(kind!!),
                entry["usable"] as? Boolean,
            )
        }
    }
}
