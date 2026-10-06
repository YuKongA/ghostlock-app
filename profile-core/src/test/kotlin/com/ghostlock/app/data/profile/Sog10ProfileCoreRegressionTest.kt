package com.ghostlock.app.data.profile

import com.ghostlock.app.data.HoconSupport
import com.ghostlock.app.data.NativeProfileDocument
import com.ghostlock.app.data.ProfileLayout
import com.ghostlock.app.data.ValueMap
import com.ghostlock.app.data.asValueMap
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
    fun `SOG10 profile keeps explicit nulls and round trips through GLKv3`() {
        val profiles = File(repoRoot(), "app/src/main/assets/kernel_profiles")
        val builtin = parse(File(profiles, "$release.conf"))
        assertTrue(builtin.containsKey("kernel_phys_load"))
        assertNull(builtin["kernel_phys_load"])
        val cred = requireNotNull(builtin["cred"].asValueMap())
        assertTrue(cred.containsKey("usage_offset"))
        assertNull(cred["usage_offset"])
        assertEquals(47529984L, (builtin["offset"].asValueMap()!!["empty_zero_page"] as Number).toLong())
        assertEquals(setOf("multicast_waiter"), builtin["route"].asValueMap()!!.keys)
        assertNull(builtin["fallback"])

        val tuning = parse(File(profiles, "execution-tuning.conf"))["execution"].asValueMap()
        val route = "multicast_waiter"

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
        assertEquals(emptyList<ConfigError>(), ProfileResolver.validateMerged(merged, route))

        fun document(profile: ValueMap) = NativeProfileDocument.from(
            release = release,
            route = route,
            value = { path -> ProfileResolver.nativeValue(profile, route, path) },
            text = { path -> ProfileResolver.nativeText(profile, path) },
            bool = { path -> ProfileResolver.nativeBool(profile, path) },
        )
        val bytes = Glkv3Encoder.encode(NativeProfileGlkv3Adapter.adapt(document(merged)))
        // Explicit null is a completeness marker, not a guessed zero/address.
        val previouslySparse = parse(File(profiles, "$release.conf"))
        previouslySparse.remove("kernel_phys_load")
        previouslySparse["cred"].asValueMap()!!.remove("usage_offset")
        assertArrayEquals(
            bytes,
            Glkv3Encoder.encode(
                NativeProfileGlkv3Adapter.adapt(document(resolve(previouslySparse))),
            ),
        )
        // GLKv3 has no container header: the root is a MessagePack map marker.
        assertTrue(
            (bytes[0].toInt() and 0xff and 0xf0) == 0x80 ||
                bytes[0] == 0xDE.toByte() ||
                bytes[0] == 0xDF.toByte(),
        )

        val decoded = requireNotNull(Glkv3Decoder.decode(bytes))
        assertEquals(release, decoded.release)
        assertEquals("multicast_waiter", decoded.route)
        /* HOCON refactor: no common owner; the kernel scalars are root values. */
        assertEquals(5uL, decoded.kernelMajor)
        assertEquals(Glkv3Value.Str("mcast_rootchild"), entry(decoded, "backend.cve_2026_43499", "steps"))
        assertNull(entryOrNull(decoded, "backend.cve_2026_43499.abi.kernel", "kernel_phys_load"))
        assertEquals(
            Glkv3Value.UInt(0u),
            entry(decoded, "backend.cve_2026_43499.abi.cred", "usage_offset"),
        )
        assertEquals(
            Glkv3Value.UInt((-274698454400L).toULong()),
            entry(decoded, "backend.cve_2026_43499.cred", "ref0_image"),
        )
        assertEquals(
            Glkv3Value.UInt((-274696707824L).toULong()),
            entry(decoded, "backend.cve_2026_43499.cred", "ref1_image"),
        )
        assertEquals(
            Glkv3Value.UInt((-274698453008L).toULong()),
            entry(decoded, "backend.cve_2026_43499.cred", "ref2_image"),
        )
        assertEquals(
            Glkv3Value.UInt((-274698454232L).toULong()),
            entry(decoded, "backend.cve_2026_43499.cred", "ref3_image"),
        )
        assertEquals(
            Glkv3Value.UInt(35027464u),
            entry(decoded, "backend.cve_2026_43499.abi.offset", "selinux_blob_sizes"),
        )
        assertEquals(
            Glkv3Value.UInt(35018112u),
            entry(decoded, "backend.cve_2026_43499.abi.offset", "security_hook_heads"),
        )

        val routeSection = "backend.cve_2026_43499.route.multicast_waiter"
        assertEquals(Glkv3Value.UInt(96u), entry(decoded, routeSection, "waiter_off"))
        assertEquals(Glkv3Value.UInt(264u), entry(decoded, routeSection, "buffer_size"))
        assertEquals(Glkv3Value.UInt(48u), entry(decoded, routeSection, "task_offset"))
        assertEquals(Glkv3Value.UInt(56u), entry(decoded, routeSection, "lock_offset"))
        assertTrue(bytes.isNotEmpty())
    }

    private fun entry(document: Glkv3Document, section: String, key: String): Glkv3Value =
        document.sections
            .first { it.name == section }
            .entries
            .first { it.key == key }
            .value

    private fun entryOrNull(
        document: Glkv3Document,
        section: String,
        key: String,
    ): Glkv3Value? = document.sections
        .firstOrNull { it.name == section }
        ?.entries
        ?.firstOrNull { it.key == key }
        ?.value

    private fun parse(file: File): ValueMap {
        val parsed = requireNotNull(
            HoconSupport.parseValue(file.readText()).asValueMap(),
        ) { "cannot parse ${file.path}" }
        ProfileLayout.applyNormalize(parsed)
        return parsed
    }

    private fun repoRoot(): File {
        var current = File(System.getProperty("user.dir")).canonicalFile
        repeat(5) {
            if (File(current, "app/src/main/assets/kernel_profiles").isDirectory) return current
            current = current.parentFile ?: error("cannot locate repository root")
        }
        error("cannot locate repository root")
    }
}
