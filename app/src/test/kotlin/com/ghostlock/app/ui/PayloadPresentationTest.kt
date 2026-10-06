package com.ghostlock.app.ui

import com.ghostlock.app.R
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The custom-execution projections: argv semantics, the values only the selected
 * tier contributes, the run blockers and the pre-run summary.
 *
 * There is no hash anywhere in the model by the user's ruling ("assume the user
 * knows what they passed in"); the wire keeps `*.sha256`, this page just never
 * emits it — the tests below pin exactly that.
 */
class PayloadPresentationTest {

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
            listOf("exec.command"),
            rows(PayloadTier.Exec).single { it.selected }.fieldKeys,
        )
        assertEquals(listOf("script.pick"), rows(PayloadTier.Script).single { it.selected }.actionKeys)
        assertEquals(listOf("ko.pick"), rows(PayloadTier.Ko).single { it.selected }.actionKeys)
        /* The default is the first row of the group (the initial selection). */
        assertNull(rows(null).first().tier)
    }

    /**
     * The default tier's submenu: the SYSTEM DEFAULT first (initial selection),
     * then only the managers that are actually installed — a row can never offer
     * a target that cannot open.
     */
    @Test
    fun `the manager picker lists the system default and only installed managers`() {
        val none = payloadManagerRows(PayloadDraft(), emptySet())
        assertEquals(1, none.size)
        assertNull(none.single().manager)
        assertTrue(none.single().selected)
        assertEquals("me.weishu.kernelsu", none.single().packageName)

        val installed = setOf("me.weishu.kernelsu", "com.resukisu.resukisu")
        val rows = payloadManagerRows(PayloadDraft(), installed)
        assertEquals(listOf(null, RootManager.KernelSU, RootManager.ReSukiSU), rows.map { it.manager })
        /* Not installed: never offered. */
        assertTrue(rows.none { it.manager == RootManager.KowSU })
        /* Still the default until the user picks one. */
        assertEquals(1, rows.count { it.selected })

        val picked = payloadManagerRows(PayloadDraft(rootManager = RootManager.ReSukiSU), installed)
        assertEquals(RootManager.ReSukiSU, picked.single { it.selected }.manager)
        assertEquals("com.resukisu.resukisu", picked.single { it.selected }.packageName)
    }

    /** Only the default tier carries the pick; the run's own manager applies otherwise. */
    @Test
    fun `the picked manager is launched, and only from the default tier`() {
        assertNull(payloadLaunchManager(PayloadDraft()))
        assertEquals(
            RootManager.KowSU,
            payloadLaunchManager(PayloadDraft(rootManager = RootManager.KowSU)),
        )
        /* A custom-execution tier does not touch the manager choice. */
        assertNull(payloadLaunchManager(PayloadDraft(tier = PayloadTier.Exec, rootManager = RootManager.KowSU)))
    }

    @Test
    fun `the default tier is ready by definition`() {
        val draft = PayloadDraft()
        /* Wire: the emitter is commented out (user ruling 2026-10-05), so the
         * payloadValues(draft).isEmpty() assertion that used to live here is
         * withdrawn with it. */
        /* Nothing to block: the default always runs. */
        assertTrue(payloadBlockers(draft).isEmpty())
        assertEquals("payload: default flow (no custom content)", payloadRunLogLine(draft))
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

    /*
     * COMMENTED OUT (user ruling 2026-10-05): the payload emitter itself is
     * withdrawn (see PayloadPresentation.payloadValues), so there is no tier-value
     * assertion to make. Restore with the emitter and, when batch (b) lands, its
     * call site.
     *
     * @Test
     * fun `no tier contributes a payload value while the feature is paused`() {
     *     val drafts = listOf(
     *         PayloadDraft(),
     *         PayloadDraft(tier = PayloadTier.Exec, execCommand = "id -u"),
     *         PayloadDraft(tier = PayloadTier.Script, scriptName = "s.sh", scriptPath = "p/s.sh"),
     *         PayloadDraft(
     *             tier = PayloadTier.Ko,
     *             koEntries = listOf(PayloadKoEntry("a.ko", "payload/ko/a.ko")),
     *         ),
     *     )
     *     for (draft in drafts) {
     *         assertTrue(payloadValues(draft).isEmpty())
     *     }
     * }
     */

    /** Blockers are what the run gate shows; the page itself has no check list. */
    @Test
    fun `only a custom tier that is not ready has blockers`() {
        /* Ready drafts block nothing. */
        assertTrue(payloadBlockers(PayloadDraft(tier = PayloadTier.Exec, execCommand = "id")).isEmpty())
        assertTrue(
            payloadBlockers(
                PayloadDraft(tier = PayloadTier.Script, scriptName = "s.sh", scriptPath = "p/s.sh"),
            ).isEmpty(),
        )
        assertTrue(
            payloadBlockers(
                PayloadDraft(tier = PayloadTier.Ko, koEntries = listOf(PayloadKoEntry("a.ko", "p/a.ko"))),
            ).isEmpty(),
        )

        /* Not ready: each tier names its own reason, in one place. */
        assertEquals(
            listOf(R.string.payload_block_command_empty),
            payloadBlockers(PayloadDraft(tier = PayloadTier.Exec)).map { it.resId },
        )
        assertEquals(
            listOf(R.string.payload_script_none),
            payloadBlockers(PayloadDraft(tier = PayloadTier.Script)).map { it.resId },
        )
        assertEquals(
            listOf(R.string.payload_ko_none),
            payloadBlockers(PayloadDraft(tier = PayloadTier.Ko)).map { it.resId },
        )

        /* More than eight modules blocks, and the reason carries the bound. */
        val many = (0..PAYLOAD_MAX_KO).map { PayloadKoEntry("k$it.ko", "payload/ko/k$it.ko") }
        val blocker = payloadBlockers(PayloadDraft(tier = PayloadTier.Ko, koEntries = many)).single()
        assertEquals(R.string.payload_block_ko_too_many, blocker.resId)
        assertEquals(listOf(PAYLOAD_MAX_KO.toString()), blocker.args)
    }

    @Test
    fun `the pre-run log line names what will happen`() {
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
                    koEntries = listOf(PayloadKoEntry("a.ko", "payload/ko/a.ko")),
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
        assertTrue(rows.none { it.label == "sha256" })
        assertEquals("default", payloadHeaderRows(PayloadDraft()).first { it.label == "tier" }.value)
    }
}
