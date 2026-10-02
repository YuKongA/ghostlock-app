package com.ghostlock.app.data.profile

import com.ghostlock.app.data.HoconSupport
import com.ghostlock.app.data.NativeProfileDocument
import com.ghostlock.app.data.ValueMap
import com.ghostlock.app.data.asValueMap
import com.ghostlock.app.data.route.MulticastConfig
import com.ghostlock.app.data.route.RouteKind
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class Sog10ProfileCoreRegressionTest {
    private val release = "5.15.189-android13-8-00004-g1c3825f8ac0a-ab14110541"

    @Test
    fun `SOG10 profile keeps explicit nulls and round trips through GLK1`() {
        val profiles = File(repoRoot(), "app/src/main/assets/kernel_profiles")
        val builtin = parse(File(profiles, "$release.conf"))
        assertTrue(builtin.containsKey("kernel_phys_load"))
        assertNull(builtin["kernel_phys_load"])
        val cred = requireNotNull(builtin["cred"].asValueMap())
        assertTrue(cred.containsKey("usage_offset"))
        assertNull(cred["usage_offset"])
        assertEquals(47529984L, (builtin["offset"].asValueMap()!!["empty_zero_page"] as Number).toLong())
        assertEquals(setOf("multicast_waiter"), builtin["route"].asValueMap()!!.keys)
        assertEquals(setOf("to"), builtin["fallback"].asValueMap()!!.keys)

        val tuning = parse(File(profiles, "execution-tuning.conf"))["execution"].asValueMap()
        val route = "multicast_waiter"
        val fallback = "none"

        val presets = RouteKind.entries.mapNotNull { kind ->
            val file = File(profiles, "execution-${kind.token.replace('_', '-')}.conf")
            if (!file.isFile) return@mapNotNull null
            val preset = parse(file)
            kind.token to requireNotNull(
                preset["execution"].asValueMap()?.get("routes").asValueMap()
                    ?.get(kind.token).asValueMap(),
            )
        }.toMap()

        fun resolve(profile: ValueMap) = ProfileMerger.resolveMerged(
            deviceRelease = release,
            builtin = profile,
            imported = null,
            overrides = null,
            tuningExecution = tuning,
            pair = CpuPairView(0, 1),
            routePresets = presets,
        )
        val merged = resolve(builtin)
        assertEquals(emptyList<ConfigError>(), ProfileResolver.validateMerged(merged, route, fallback))

        fun document(profile: ValueMap) = NativeProfileDocument.from(
            release = release,
            route = route,
            fallbackTo = fallback,
        ) { path -> ProfileResolver.nativeValue(profile, route, fallback, path) }
        val bytes = document(merged).toBinary()
        // Explicit null is a completeness marker, not a guessed zero/address.
        val previouslySparse = parse(File(profiles, "$release.conf"))
        previouslySparse.remove("kernel_phys_load")
        previouslySparse["cred"].asValueMap()!!.remove("usage_offset")
        assertArrayEquals(bytes, document(resolve(previouslySparse)).toBinary())
        assertArrayEquals(byteArrayOf(0x21, 0x07, 0x00, 0x0D), bytes.copyOfRange(0, 4))

        val decoded = requireNotNull(NativeProfileDocument.fromBinary(bytes))
        assertEquals(release, decoded.release)
        assertEquals(3u, decoded.routeKind)
        assertEquals(0u, decoded.fallbackRoute)
        assertEquals(1u, decoded.recommendShizuku)
        assertNull(decoded.kernelPhysLoad)
        assertEquals(0u, decoded.cred.usageOffset)
        assertEquals(-274698454400L, decoded.cred.ref0Image.toLong())
        assertEquals(-274696707824L, decoded.cred.ref1Image.toLong())
        assertEquals(-274698453008L, decoded.cred.ref2Image.toLong())
        assertEquals(-274698454232L, decoded.cred.ref3Image.toLong())
        assertEquals(35027464uL, decoded.kernelOffset.selinuxBlobSizes)
        assertEquals(35018112uL, decoded.kernelOffset.securityHookHeads)

        val geometry = (decoded.routeConfig as MulticastConfig).geometry
        assertEquals(96, geometry.waiterOff)
        assertEquals(264u, geometry.bufferSize)
        assertEquals(48u, geometry.taskOffset)
        assertEquals(56u, geometry.lockOffset)
        assertTrue(bytes.isNotEmpty())
    }

    private fun parse(file: File) = requireNotNull(
        HoconSupport.parseValue(file.readText()).asValueMap(),
    ) { "cannot parse ${file.path}" }

    private fun repoRoot(): File {
        var current = File(System.getProperty("user.dir")).canonicalFile
        repeat(5) {
            if (File(current, "app/src/main/assets/kernel_profiles").isDirectory) return current
            current = current.parentFile ?: error("cannot locate repository root")
        }
        error("cannot locate repository root")
    }
}
