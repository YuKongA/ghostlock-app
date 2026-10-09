package com.ghostlock.app.data.profile

import com.ghostlock.app.data.HoconSupport
import com.ghostlock.app.data.NativeProfileDocument
import com.ghostlock.app.data.ProfileLayout
import com.ghostlock.app.data.ValueMap
import com.ghostlock.app.data.asValueMap
import com.ghostlock.app.data.getValueAt
import com.ghostlock.app.data.mutableChild
import com.ghostlock.app.data.route.RouteKind
import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.component.CombinationCatalog
import com.ghostlock.app.data.component.FrontendKind
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.math.BigInteger
import java.security.MessageDigest

/**
 * A6-4 segment three end to end: a real bundled profile, resolved through the
 * production chain (canonicalize -> runtime -> NativeProfileDocument.from ->
 * adapter -> encoder), emits the payload family, and the digest the document
 * declares is RE-COMPUTED from the bytes that came back out of the wire.
 *
 * The binding, not the field, is the acceptance: a section that merely carries a
 * sha256 string proves nothing, so every assertion below digests the decoded bin
 * with an independent implementation (BigInteger hex, not the production table).
 */
class PayloadCarriageTest {
    private val release = "5.15.189-android13-8-00004-g1c3825f8ac0a-ab14110541"

    /** Frozen fixture inputs: the emitted bytes are the native side's input. */
    private companion object {
        private const val FIXTURE_SCRIPT = "echo ghostlock-fixture\n"
        private const val FIXTURE_RELEASE = "7.0-fixture-payload"
    }

    /**
     * Which terminal receives which deliverable. This is the explicit table the
     * batch asked for: a terminal's payloads are selected per terminal, and the
     * device-side resolve owns the content choice (the KMI label for a module,
     * the finishing script for the root child). Here the script is the REAL one
     * from tools/lkm/ghostlock/root_cmd.sh; the module is a fixture unless a
     * built module happens to be present.
     */
    private fun deliverables(): Map<String, List<HandoffPayload>> {
        val script = HandoffPayload.of(
            "root_child_script",
            "script",
            File(repoRoot(), "tools/lkm/ghostlock/root_cmd.sh").readBytes(),
        )
        val module = HandoffPayload.of("root_child_module", "module", moduleBytes())
        val forwarded = HandoffPayload.of(
            "umh_forward_module",
            "module",
            ByteArray(96) { index -> (index + 7).toByte() },
        )
        val table = linkedMapOf<String, MutableList<HandoffPayload>>()
        for (terminal in FrontendKind.entries.filter { it.available }.map { it.token }) {
            table[terminal] = mutableListOf()
        }
        for (payload in listOf(script, module, forwarded)) {
            val terminal = requireNotNull(HandoffPayload.terminalOf(payload.name)) {
                "no terminal owns " + payload.name
            }
            table.getValue(terminal) += payload
        }
        return table
    }

    @Test
    fun `the declared digest binds the bytes that come back out of the wire`() {
        val table = deliverables()
        val payloads = table.values.flatten()
        val bytes = glkv3(payloads)
        val decoded = requireNotNull(Glkv3Decoder.decode(bytes)) { "document does not decode" }

        val seen = mutableSetOf<String>()
        for ((terminal, expected) in table) {
            for (payload in expected) {
                assertEquals(
                    "a payload must be owned by the terminal it is named for",
                    terminal,
                    HandoffPayload.terminalOf(payload.name),
                )
                val section = requireNotNull(decoded.sections.firstOrNull { it.name == payload.segment }) {
                    "missing payload section " + payload.segment
                }
                assertEquals(
                    "four keys, no more and no fewer",
                    HandoffPayload.KEYS.sorted(),
                    section.entries.map { it.key }.sorted(),
                )
                val bin = (section.entries.first { it.key == "bin" }.value as Glkv3Value.Bin).value
                val declared = (section.entries.first { it.key == "sha256" }.value as Glkv3Value.Str).value
                val size = (section.entries.first { it.key == "size" }.value as Glkv3Value.UInt).value
                val kind = (section.entries.first { it.key == "kind" }.value as Glkv3Value.Str).value
                assertEquals("kind", payload.kind, kind)
                assertEquals("size equals the bytes on the wire", bin.size.toULong(), size)
                assertEquals("the payload survives the round trip", payload.bytes.toList(), bin.toList())
                assertEquals("the declared digest is the digest of those bytes", digestHex(bin), declared)
                seen += payload.name
            }
        }
        assertEquals(
            "every declared payload was found on the wire",
            payloads.map { it.name }.sorted(),
            seen.sorted(),
        )
        assertTrue(
            "the family is present and non-empty",
            decoded.sections.count { it.name.startsWith(HandoffPayload.SEGMENT_PREFIX + ".") } == payloads.size,
        )
    }

    @Test
    fun `no payload declared is no payload section, and the bytes report is printed`() {
        val withPayload = glkv3(deliverables().values.flatten())
        val withoutPayload = glkv3(emptyList())
        val decodedEmpty = requireNotNull(Glkv3Decoder.decode(withoutPayload))

        assertTrue(
            "an absent payload is never declared",
            decodedEmpty.sections.none { it.name.startsWith(HandoffPayload.SEGMENT_PREFIX + ".") },
        )
        for (payload in deliverables().values.flatten()) {
            assertTrue(
                "no slot may appear without a payload: " + payload.segment,
                decodedEmpty.sections.none { it.name == payload.segment },
            )
        }
        assertTrue("the family adds bytes only when it is declared", withPayload.size > withoutPayload.size)
        val hex = withPayload.joinToString("") { byte -> String.format("%02x", byte) }
        /* Sanity, not decoration: a harness that silently emitted an empty
         * document would make every byte number below meaningless. */
        assertTrue(
            "the harness must emit the real profile document, not a stub",
            decodedEmpty.sections.size >= 8,
        )
        println("payload-doc base_sections=" + decodedEmpty.sections.size)
        println("payload-doc bytes=" + withPayload.size + " without=" + withoutPayload.size +
            " delta=" + (withPayload.size - withoutPayload.size))
        println("payload-doc-sha256 " + digestHex(withPayload))
        println("payload-doc-hex " + hex)
    }

    /**
     * The native side's real input: a FROZEN minimal document carrying the
     * payload family, pinned as a test resource so the bytes cannot drift
     * silently (the pin is compared, never rewritten by this test).
     */
    @Test
    fun `the frozen fixture carries the payload family`() {
        val payload = HandoffPayload.of("root_child_script", "script", FIXTURE_SCRIPT.toByteArray())
        val bytes = smallDocument(payload)
        val hex = bytes.joinToString("") { byte -> String.format("%02x", byte) }
        val pin = File(repoRoot(), "profile-core/src/test/resources/payload-document-fixture.hex")
        println("payload-fixture bytes=" + bytes.size + " sha256=" + digestHex(bytes))
        require(pin.isFile) {
            "missing fixture pin " + pin.path + "; its content is:\n" + hex
        }
        assertEquals(
            "the frozen fixture must match the current encoder output",
            pin.readText().trim(),
            hex,
        )
    }

    /**
     * A minimal document with frozen literal inputs: every value is a literal
     * here, so the emitted bytes are stable and can be pinned as a fixture.
     */
    private fun smallDocument(payload: HandoffPayload): ByteArray {
        val values = mapOf(
            "kernel_major" to 5L,
            "cred.copy_size" to 136L,
            "offset.init_cred" to 136120L,
            "route.multicast_waiter.waiter_off" to 96L,
        )
        val texts = mapOf("backend.kind" to "cve_2026_43499")
        val document = NativeProfileDocument.from(
            release = FIXTURE_RELEASE,
            route = "multicast_waiter",
            value = { path -> values[path] },
            text = { path -> texts[path] },
            bool = { _ -> null },
            payloads = listOf(payload),
        )
        return Glkv3Encoder.encode(NativeProfileGlkv3Adapter.adapt(document))
    }

    /**
     * The exporter's own chain: asset (includes resolved, normalised) -> merge ->
     * document -> adapter -> wire. Using the exporter's shape matters: a looser
     * harness can emit a document whose sections are all empty, which would make
     * the byte numbers below meaningless.
     */
    private fun glkv3(payloads: List<HandoffPayload>): ByteArray {
        val merged = mergedProfile()
        val srcDir = File(repoRoot(), "app/src/main/assets/profile")
        val builtin = load(File(srcDir, release + ".conf"), srcDir)
        val tuning = load(File(srcDir, "execution-tuning.conf"), srcDir)["execution"].asValueMap()
        val routePresets = RouteKind.entries.mapNotNull { kind ->
            val preset = File(srcDir, "execution-" + kind.token.replace('_', '-') + ".conf")
            if (!preset.isFile) return@mapNotNull null
            val group = load(preset, srcDir)["execution"].asValueMap()
                ?.get("routes").asValueMap()
                ?.get(kind.token).asValueMap()
            group?.let { kind.token to it }
        }.toMap()
        /* The route the document carries comes from the profile's own branch: this
         * harness needs it for the route-scoped accessors, not to re-derive the
         * combination (the exporter does that through the catalogue). */
        val route = merged["route"].asValueMap()?.keys?.firstOrNull()
        val document = NativeProfileDocument.from(
            release = release,
            route = route,
            value = { path -> ProfileResolver.nativeValue(merged, route, path) },
            text = { path -> ProfileResolver.nativeText(merged, path) },
            bool = { path -> ProfileResolver.nativeBool(merged, path) },
            raw = { path -> merged.getValueAt(path) },
            payloads = payloads,
        )
        return Glkv3Encoder.encode(NativeProfileGlkv3Adapter.adapt(document))
    }

    /** The exporter's merge inputs, assembled exactly like ProfileExporter does. */
    private fun mergedProfile(): ValueMap {
        val srcDir = File(repoRoot(), "app/src/main/assets/profile")
        val builtin = load(File(srcDir, release + ".conf"), srcDir)
        val tuning = load(File(srcDir, "execution-tuning.conf"), srcDir)["execution"].asValueMap()
        val routePresets = RouteKind.entries.mapNotNull { kind ->
            val preset = File(srcDir, "execution-" + kind.token.replace('_', '-') + ".conf")
            if (!preset.isFile) return@mapNotNull null
            val group = load(preset, srcDir)["execution"].asValueMap()
                ?.get("routes").asValueMap()
                ?.get(kind.token).asValueMap()
            group?.let { kind.token to it }
        }.toMap()
        val merged = ProfileMerger.resolveMerged(
            deviceRelease = release,
            builtin = builtin,
            imported = null,
            overrides = null,
            tuningExecution = tuning,
            pair = CpuPairView(main = 0, consumer = 1),
            routePresets = routePresets,
        )
        /* The selection carrier, exactly as the exporter injects it: without it
         * the document declares no combination and the backend-owned sections are
         * legitimately dropped (measured: 1 section / 201 B instead of the real
         * profile document), which would make every byte number below noise. */
        merged.mutableChild("backend")["kind"] = BackendKind.Cve2026_43499.token
        val declaredRoute = merged["route"].asValueMap()?.keys?.firstOrNull()
        /* The terminal comes from the catalogue default, never from a literal:
         * the payload names must lead with the terminal that actually runs
         * them, and the default is the one this path selects. */
        val terminal = NativeProfileGlkv3Adapter.DEFAULT_TERMINAL
        val specs = CombinationCatalog.forBackend(BackendKind.Cve2026_43499)
            .filter { it.available && it.route?.token == declaredRoute && it.terminal.token == terminal }
        /* Two catalogue rows can share a route and a terminal (the shizuku token
         * maps onto root_child), and the token is an INTERNAL normalisation key
         * that is no longer emitted (M5), so those rows yield byte-identical
         * documents; which one the App picked is not this harness's subject. What
         * must not happen is an EMPTY set: that would silently emit a document
         * with no combination at all. */
        require(specs.isNotEmpty()) {
            "no available 43499 combination for route " + declaredRoute +
                " and terminal " + terminal
        }
        merged.mutableChild("backend")["steps"] = specs.first().token
        return merged
    }

    /** HOCON with its includes resolved (the exporter's reader is its own file). */
    private fun load(file: File, srcDir: File): ValueMap {
        val text = StringBuilder()
        for (line in file.readLines()) {
            val include = Regex("^\\s*include\\s+\"([^\"]+)\"\\s*$").find(line)
            if (include == null) {
                text.append(line).append('\n')
            } else {
                text.append(File(srcDir, include.groupValues[1]).readText()).append('\n')
            }
        }
        val root = requireNotNull(HoconSupport.parseValue(text.toString()).asValueMap()) {
            "cannot parse " + file.path
        }
        /* The R3 wrapper is unwrapped BEFORE normalizing: leaving it on makes the
         * normalized map nearly empty and every section silently vanishes. */
        val parsed = HoconSupport.unwrapProfileDocument(root).asValueMap() ?: root
        ProfileLayout.applyNormalize(parsed)
        return parsed
    }

    /** Independent of the production hex table on purpose (non-self-proving). */
    private fun digestHex(bytes: ByteArray): String =
        BigInteger(1, MessageDigest.getInstance("SHA-256").digest(bytes))
            .toString(16)
            .padStart(64, '0')

    /** A built module when the tree has one; otherwise a deterministic fixture. */
    private fun moduleBytes(): ByteArray {
        val generated = File(repoRoot(), "build/app/generated/lkmAssets/lkm")
        val label = generated.listFiles()?.sortedBy { it.name }?.firstOrNull { it.isDirectory }
        val module = label?.let { File(it, "ghostlock.ko") }
        if (module != null && module.isFile) {
            println("payload-doc module source=" + label.name + " bytes=" + module.length())
            return module.readBytes()
        }
        println("payload-doc module source=fixture (no built module in the tree)")
        return ByteArray(512) { index -> (index % 251).toByte() }
    }

    private fun repoRoot(): File {
        var current = File(System.getProperty("user.dir")).canonicalFile
        repeat(5) {
            if (File(current, "app/src/main/assets/profile").isDirectory) return current
            current = current.parentFile ?: error("cannot locate repository root")
        }
        error("cannot locate repository root")
    }
}
