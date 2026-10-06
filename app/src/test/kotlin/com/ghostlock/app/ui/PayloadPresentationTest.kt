package com.ghostlock.app.ui

import com.ghostlock.app.R
import com.ghostlock.app.data.plugin.PluginValue
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * payload batch (a) projections: argv semantics, the values only the selected
 * tier contributes, the local pre-checks and the pre-run summary.
 */
class PayloadPresentationTest {

    private val sha = "c".repeat(64)

    @Test
    fun `the tier list is a radio group whose expansion belongs to the selected row`() {
        fun rows(tier: PayloadTier?) = payloadTierRows(PayloadDraft(tier = tier))
        for (tier in listOf(null, PayloadTier.Exec, PayloadTier.Script, PayloadTier.Ko)) {
            val list = rows(tier)
            assertEquals(4, list.size)
            /* Exactly one selection ... */
            assertEquals(1, list.count { it.selected })
            /* ... and exactly that row is the one that opens. */
            assertEquals(1, list.count { it.expanded })
            val selected = list.single { it.selected }
            assertEquals(tier, selected.tier)
            assertTrue(selected.expanded)
            for (collapsed in list.filterNot { it.selected }) {
                assertFalse(collapsed.expanded)
                assertTrue(collapsed.fieldKeys.isEmpty())
                assertTrue(collapsed.actionKeys.isEmpty())
            }
        }
        /* The default opens nothing: there is nothing to configure. */
        val default = rows(null).single { it.selected }
        assertTrue(default.fieldKeys.isEmpty())
        assertTrue(default.actionKeys.isEmpty())
        /* Each custom tier opens exactly its own controls, under its own row. */
        assertEquals(
            listOf("exec.command", "exec.sha256"),
            rows(PayloadTier.Exec).single { it.selected }.fieldKeys,
        )
        assertEquals(listOf("script.pick"), rows(PayloadTier.Script).single { it.selected }.actionKeys)
        assertEquals(listOf("ko.pick"), rows(PayloadTier.Ko).single { it.selected }.actionKeys)
        /* The default is the first row of the group (the initial selection). */
        assertNull(rows(null).first().tier)
    }

    @Test
    fun `the default tier emits nothing, needs no authorisation and still has a summary`() {
        val draft = PayloadDraft()
        /* Wire: the default contributes NO payload keys at all. */
        assertTrue(payloadValues(draft).isEmpty())
        /* Authorisation: there is nothing custom to authorise. */
        assertFalse(draft.needsAuthorisation)
        assertTrue(PayloadDraft(tier = PayloadTier.Exec).needsAuthorisation)
        /* The pre-run summary is visible for the default too. */
        assertEquals(
            "payload: default flow (no custom content)",
            payloadRunLogLine(draft),
        )
        /* Report: context only, never an error. */
        val messages = payloadMessages(draft)
        assertEquals(1, messages.size)
        assertEquals(PluginIssueLevel.Info, messages.single().level)
        assertEquals(R.string.payload_check_default, messages.single().resId)
        /* Header names the default explicitly. */
        assertEquals("default", payloadHeaderRows(draft).single().value)
    }

    @Test
    fun `the command is split on whitespace, never by a shell`() {
        assertEquals(listOf("id", "-u"), payloadArgv("  id   -u  "))
        assertEquals(listOf("id", "-u"), payloadArgv("id -u"))
        assertTrue(payloadArgv("   ").isEmpty())
        /* Quotes are NOT interpreted: they stay part of the argument. */
        assertEquals(
            listOf("echo", "\"a", "b\""),
            payloadArgv("echo \"a b\""),
        )
    }

    @Test
    fun `only the selected tier contributes values`() {
        assertTrue(payloadValues(PayloadDraft()).isEmpty())

        val exec = payloadValues(
            PayloadDraft(tier = PayloadTier.Exec, execCommand = "id -u", execSha256 = sha),
        )
        assertEquals(PluginValue.Str("exec"), exec["tier"])
        assertEquals(PluginValue.Str("id -u"), exec["exec.command"])
        assertEquals(PluginValue.Str(sha), exec["exec.sha256"])
        assertTrue(exec.keys.none { it.startsWith("script") || it.startsWith("ko.") })

        /* A blank hash is omitted, never written empty. */
        val unpinned = payloadValues(
            PayloadDraft(tier = PayloadTier.Exec, execCommand = "id", execSha256 = "  "),
        )
        assertTrue(unpinned.keys.none { it == "exec.sha256" })

        val ko = payloadValues(
            PayloadDraft(
                tier = PayloadTier.Ko,
                koEntries = listOf(
                    PayloadKoEntry("a.ko", "payload/ko/a.ko", sha),
                    PayloadKoEntry("b.ko", "payload/ko/b.ko", null),
                ),
            ),
        )
        assertEquals(PluginValue.UInt(2uL), ko["ko.count"])
        assertEquals(PluginValue.Str("payload/ko/a.ko"), ko["ko.0.path"])
        assertEquals(PluginValue.Str(sha), ko["ko.0.sha256"])
        assertEquals(PluginValue.Str("payload/ko/b.ko"), ko["ko.1.path"])
        assertTrue(ko.keys.none { it == "ko.1.sha256" })
    }

    @Test
    fun `local pre-checks separate blocking errors from warnings`() {
        fun levels(draft: PayloadDraft) = payloadMessages(draft)

        /* Nothing configured is context, not an error. */
        assertTrue(levels(PayloadDraft()).all { it.level == PluginIssueLevel.Info })
        /* An empty command blocks. */
        assertTrue(
            levels(PayloadDraft(tier = PayloadTier.Exec))
                .any { it.level == PluginIssueLevel.Error },
        )
        /* Quoting cannot survive the argv split: warn, and still run. */
        val quoted = levels(PayloadDraft(tier = PayloadTier.Exec, execCommand = "sh -c \"id\""))
        assertTrue(quoted.any { it.level == PluginIssueLevel.Warn && it.resId == R.string.payload_check_command_quotes })
        /* An unset hash warns; a malformed one blocks. */
        assertTrue(
            levels(PayloadDraft(tier = PayloadTier.Exec, execCommand = "id"))
                .any { it.level == PluginIssueLevel.Warn && it.resId == R.string.payload_check_hash_unset },
        )
        assertTrue(
            levels(PayloadDraft(tier = PayloadTier.Exec, execCommand = "id", execSha256 = "abc"))
                .any { it.level == PluginIssueLevel.Error },
        )
        /* A script or ko tier without content blocks. */
        assertTrue(
            levels(PayloadDraft(tier = PayloadTier.Script))
                .any { it.level == PluginIssueLevel.Error },
        )
        assertTrue(
            levels(PayloadDraft(tier = PayloadTier.Ko))
                .any { it.level == PluginIssueLevel.Error },
        )
        /* More than eight modules blocks; unverified ones warn. */
        val many = (0..PAYLOAD_MAX_KO).map { PayloadKoEntry("k$it.ko", "payload/ko/k$it.ko", sha) }
        assertTrue(
            levels(PayloadDraft(tier = PayloadTier.Ko, koEntries = many))
                .any { it.level == PluginIssueLevel.Error && it.resId == R.string.payload_check_ko_too_many },
        )
        assertTrue(
            levels(
                PayloadDraft(
                    tier = PayloadTier.Ko,
                    koEntries = listOf(PayloadKoEntry("a.ko", "payload/ko/a.ko", null)),
                ),
            ).any { it.level == PluginIssueLevel.Warn },
        )
    }

    @Test
    fun `the pre-run summary names what will happen`() {
        assertEquals(
            "payload: run as root: id -u",
            payloadRunLogLine(PayloadDraft(tier = PayloadTier.Exec, execCommand = " id -u ")),
        )
        assertEquals(
            "payload: run through the LKM: setup.sh",
            payloadRunLogLine(
                PayloadDraft(tier = PayloadTier.Script, scriptName = "setup.sh", scriptPath = "p/x"),
            ),
        )
        assertEquals(
            "payload: load 1 kernel extension(s): a.ko",
            payloadRunLogLine(
                PayloadDraft(
                    tier = PayloadTier.Ko,
                    koEntries = listOf(PayloadKoEntry("a.ko", "payload/ko/a.ko", sha)),
                ),
            ),
        )
    }

    @Test
    fun `the header shows the active tier only`() {
        val rows = payloadHeaderRows(PayloadDraft(tier = PayloadTier.Script, scriptName = "s.sh"))
        assertEquals("script", rows.first { it.label == "tier" }.value)
        assertEquals("s.sh", rows.first { it.label == "script" }.value)
        assertTrue(rows.none { it.label == "command" })
        assertEquals("default", payloadHeaderRows(PayloadDraft()).first { it.label == "tier" }.value)
    }
}
