package com.ghostlock.app.data

import com.ghostlock.app.data.profile.Glkv3Decoder
import com.ghostlock.app.data.profile.Glkv3Encoder
import com.ghostlock.app.data.profile.Glkv3Value
import com.ghostlock.app.data.profile.HandoffPayload
import com.ghostlock.app.data.profile.NativeProfileGlkv3Adapter
import com.ghostlock.app.data.route.RouteKind
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.math.BigInteger
import java.security.MessageDigest

/**
 * A6-4 device-side resolve: which terminal receives which closing artifact, and
 * that an artifact the APK does not carry stays ABSENT instead of becoming a
 * placeholder. The asset reader is injected, so these rules are checked without
 * a device.
 */
class HandoffPayloadSourceTest {
    private val release = "5.15.189-android13-8-00016-g51bba4309aac-ab14546557"
    private val script = "#!/system/bin/sh\necho hi\n".toByteArray()
    private val module = ByteArray(64) { index -> (index * 7).toByte() }

    private val assets = mapOf(
        HandoffPayloadSource.PAYLOAD_ROOT + "/" + HandoffPayloadSource.SCRIPT_ASSET to script,
        "lkm/android13-5.15/ghostlock.ko" to module,
    )

    @Test
    fun `root_child receives the script and the KMI module`() {
        val payloads = HandoffPayloadSource.forTerminal(release, "root_child") { path -> assets[path] }
        assertEquals(listOf("root_child_script", "root_child_module"), payloads.map { it.name })
        assertEquals(listOf("script", "module"), payloads.map { it.kind })
        val byName = payloads.associateBy { it.name }
        assertEquals(script.toList(), byName.getValue("root_child_script").bytes.toList())
        assertEquals(module.toList(), byName.getValue("root_child_module").bytes.toList())
        /* The declared digests bind the very bytes that were read. */
        for (payload in payloads) {
            assertEquals(digestHex(payload.bytes), payload.sha256)
        }
        assertEquals(
            "backend.cve_2026_43499.payload.root_child_script",
            byName.getValue("root_child_script").segment,
        )
    }

    @Test
    fun `an artifact the APK does not carry stays absent`() {
        val none = HandoffPayloadSource.forTerminal(release, "root_child") { null }
        assertTrue("no placeholder may be invented", none.isEmpty())
        val scriptOnly = HandoffPayloadSource.forTerminal(release, "root_child") { path ->
            assets[path].takeIf { path.endsWith(HandoffPayloadSource.SCRIPT_ASSET) }
        }
        assertEquals(listOf("root_child_script"), scriptOnly.map { it.name })
    }

    @Test
    fun `a release without a KMI keeps the script and has no module`() {
        val payloads = HandoffPayloadSource.forTerminal("not-a-release", "root_child") { path ->
            assets[path]
        }
        assertEquals(listOf("root_child_script"), payloads.map { it.name })
    }

    @Test
    fun `terminals without closing content or without a rule contribute nothing`() {
        /* umh_forward declares no closing artifact in the tree today: absence. */
        val warnings = mutableListOf<String>()
        assertTrue(
            HandoffPayloadSource.forTerminal(release, "umh_forward") { path -> assets[path] }.isEmpty(),
        )
        /* A terminal the payload rule does not admit (a combination token, a
         * misspelling) contributes nothing instead of throwing on the hot path -
         * but the refusal is REPORTED, never silent. */
        assertTrue(
            HandoffPayloadSource.forTerminal(
                release = release,
                terminal = "shizuku",
                onWarning = { warnings += it },
                readAsset = { path -> assets[path] },
            ).isEmpty(),
        )
        assertTrue(
            HandoffPayloadSource.forTerminal(
                release = release,
                terminal = "root_child_typo",
                onWarning = { warnings += it },
                readAsset = { path -> assets[path] },
            ).isEmpty(),
        )
        assertEquals(
            listOf(
                "terminal without a payload rule: shizuku",
                "terminal without a payload rule: root_child_typo",
            ),
            warnings,
        )
        assertTrue(HandoffPayloadSource.forTerminal(release, null) { path -> assets[path] }.isEmpty())
    }

    @Test
    fun `the app wiring carries the payloads into the emitted document`() {
        val payload = HandoffPayload.of("root_child_script", "script", script)
        val profile = requireNotNull(
            Profile.fromValueMap(
                release = release,
                /* A route-axis backend needs a route: a 43499 document without one
                 * is refused by design, and this test is about the payload. */
                route = RouteKind.MULTICAST_WAITER,
                value = { path -> if (path == "kernel_major") 5L else null },
                payloads = listOf(payload),
            ),
        )
        val bytes = Glkv3Encoder.encode(NativeProfileGlkv3Adapter.adapt(profile.document))
        val decoded = requireNotNull(Glkv3Decoder.decode(bytes))
        val section = requireNotNull(
            decoded.sections.firstOrNull { it.name == payload.segment },
        ) { "the payload section did not reach the wire" }
        val bin = (section.entries.first { it.key == "bin" }.value as Glkv3Value.Bin).value
        val declared = (section.entries.first { it.key == "sha256" }.value as Glkv3Value.Str).value
        assertEquals(script.toList(), bin.toList())
        assertEquals(digestHex(bin), declared)
        assertEquals(
            script.size.toULong(),
            (section.entries.first { it.key == "size" }.value as Glkv3Value.UInt).value,
        )
        assertEquals(
            "the wire shape the native authority declares",
            HandoffPayload.KEYS.sorted(),
            section.entries.map { it.key }.sorted(),
        )
    }

    /** Independent of the production hex table on purpose (non-self-proving). */
    private fun digestHex(bytes: ByteArray): String =
        BigInteger(1, MessageDigest.getInstance("SHA-256").digest(bytes))
            .toString(16)
            .padStart(64, '0')
}
