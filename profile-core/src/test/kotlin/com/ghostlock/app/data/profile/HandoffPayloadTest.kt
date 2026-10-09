package com.ghostlock.app.data.profile

import com.ghostlock.app.data.component.FrontendKind
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * A6-4 segment three: the payload name rule, the key and kind sets, and the
 * digest binding, every one of them checked against the pin the native side
 * exports (src/core/tests/payload-manifest.pin.tsv) instead of only against
 * this file's own constants.
 */
class HandoffPayloadTest {

    @Test
    fun `the rule matches the native exported pin`() {
        val pin = pinRows()
        assertEquals("segment", HandoffPayload.SEGMENT_PREFIX, pin.single("segment").second)
        assertEquals(
            "the admitted terminals are the selectable ones, derived not written",
            FrontendKind.entries.filter { it.available }.map { it.token }.sorted(),
            pin.filter { it.first == "terminal" }.map { it.second }.sorted(),
        )
        assertEquals(
            "keys",
            HandoffPayload.KEYS,
            pin.filter { it.first == "key" }.map { it.second },
        )
        assertEquals(
            "kinds",
            HandoffPayload.KINDS,
            pin.filter { it.first == "kind" }.map { it.second },
        )
        assertEquals(
            "suffix_max",
            HandoffPayload.SUFFIX_MAX.toString(),
            pin.single("suffix_max").second,
        )
        assertEquals("bare_name", "rejected", pin.single("bare_name").second)
    }

    @Test
    fun `a name is a selectable terminal plus a bounded suffix`() {
        /* Every selectable terminal works - the table is iterated, never written. */
        for (terminal in HandoffPayload.TERMINALS) {
            assertTrue(terminal + "_script is admissible", HandoffPayload.nameIsValid(terminal + "_script"))
        }
        assertTrue(HandoffPayload.nameIsValid("root_child_script"))
        assertTrue(HandoffPayload.nameIsValid("umh_forward_module"))
        assertTrue("a suffix may carry underscores and digits", HandoffPayload.nameIsValid("root_child_a_1"))
        assertTrue("the suffix bound is inclusive", HandoffPayload.nameIsValid("root_child_" + "a".repeat(16)))
    }

    @Test
    fun `bare, free and combination names are refused`() {
        val refused = listOf(
            "root_child",             /* bare: one concept, two shapes */
            "umh_forward",            /* bare */
            "root_child_",            /* empty suffix */
            "root_child_" + "a".repeat(17),  /* suffix over the bound */
            "root_child_Script",      /* upper case is not in the suffix set */
            "root_child-sc",          /* dash is not in the suffix set */
            "device_script",          /* free name: no declared terminal */
            "shizuku_script",         /* a combination token, not a terminal */
            "_script",
            "",
        )
        for (name in refused) {
            assertFalse("must be refused: " + name, HandoffPayload.nameIsValid(name))
        }
    }

    @Test
    fun `building a payload refuses what the native reader would refuse`() {
        val ok = HandoffPayload.of("root_child_script", "script", "echo hi".toByteArray())
        assertEquals("script", ok.kind)
        assertEquals("echo hi".toByteArray().size.toUInt(), ok.size)
        assertEquals("backend.cve_2026_43499.payload.root_child_script", ok.segment)

        val failures = listOf<() -> Unit>(
            { HandoffPayload.of("root_child", "script", byteArrayOf(1)) },
            { HandoffPayload.of("device_script", "script", byteArrayOf(1)) },
            { HandoffPayload.of("root_child_script", "kernel", byteArrayOf(1)) },
            { HandoffPayload.of("root_child_script", "script", ByteArray(0)) },
        )
        for (failure in failures) {
            val error = runCatching(failure).exceptionOrNull()
            assertTrue("every refusal is an IllegalArgumentException: " + error, error is IllegalArgumentException)
        }
    }

    @Test
    fun `the digest is the published sha256 of the bytes`() {
        /* FIPS 180-2 vector: sha256("abc"). */
        val payload = HandoffPayload.of("root_child_script", "script", "abc".toByteArray())
        assertEquals(
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            payload.sha256,
        )
        assertEquals(64, payload.sha256.length)
        assertTrue("lower-case hex only", payload.sha256.all { it in "0123456789abcdef" })
        /* A second vector, one byte longer (the digest must move with the bytes). */
        val longer = HandoffPayload.of("root_child_script", "script", "abcd".toByteArray())
        assertEquals(
            "88d4266fd4e6338d13b845fcf289579d209c897823b9217da3e161936f031589",
            longer.sha256,
        )
    }

    private fun pinRows(): List<Pair<String, String>> {
        val file = File(repoRoot(), "src/core/tests/payload-manifest.pin.tsv")
        require(file.isFile) { "missing native pin: " + file.path }
        return file.readLines()
            .map { it.trimEnd() }
            .filter { it.isNotBlank() && !it.startsWith("#") }
            .map { line ->
                val parts = line.split('\t')
                require(parts.size == 2) { "malformed pin row: " + line }
                parts[0] to parts[1]
            }
    }

    private fun List<Pair<String, String>>.single(kind: String): Pair<String, String> =
        single { it.first == kind }

    private fun repoRoot(): File {
        var current = File(System.getProperty("user.dir")).canonicalFile
        repeat(5) {
            if (File(current, "app/src/main/assets/profile").isDirectory) return current
            current = current.parentFile ?: error("cannot locate repository root")
        }
        error("cannot locate repository root")
    }
}
