package com.ghostlock.app.ui.screen.module.backends

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** 整脚本渲染：载荷顺序、收尾语句、LOG 指向 memfd、路径转义。 */
class ScriptRenderTest {

    private val log = "/proc/4242/fd/9"

    @Test
    fun rendersWholeScriptWithLogChannel() {
        val script = FinishState().toRootScript(null, emptyMap(), log)
        assertTrue(script.startsWith("#!/system/bin/sh"))
        assertTrue(script.contains("LOG='" + log + "'"))
        assertTrue(script.contains("exec 3>>\"\$LOG\""))
        assertTrue(script.contains("log 'finish start'"))
        assertTrue(script.contains("log 'finish done'"))
        assertTrue("载荷与收尾同段", script.indexOf("finish start") < script.indexOf("finish done"))
    }

    @Test
    fun finishSwitchesAreScriptStatements() {
        val quiet = FinishState().toRootScript(null, emptyMap(), log)
        assertFalse(quiet.contains("setenforce 1"))
        assertFalse(quiet.contains("ctl.restart zygote"))

        val loud = FinishState(restoreSelinux = true, softRebootAfterFinish = true)
            .toRootScript(null, emptyMap(), log)
        assertTrue(loud.contains("setenforce 1"))
        assertTrue(loud.contains("setprop ctl.restart zygote"))
        assertTrue("收尾在载荷之后", loud.indexOf("setenforce 1") > loud.indexOf("finish start"))
    }

    @Test
    fun payloadOrderFollowsSlotOrder() {
        val state = FinishState(
            payloads = listOf(
                PayloadSlot("cmd", PayloadKind.Command, command = "id"),
                PayloadSlot("root", PayloadKind.Root),
            ),
        )
        val script = state.toRootScript(null, emptyMap(), log)
        val cmd = script.indexOf("\nid\n")
        val root = script.indexOf("start root manager")
        assertTrue("cmd 在 root 之前", cmd in 0 until root)
    }

    @Test
    fun rendersDuplicateCommandSlots() {
        val state = FinishState(
            payloads = listOf(
                PayloadSlot("c1", PayloadKind.Command, command = "id"),
                PayloadSlot("c2", PayloadKind.Command, command = "uname -a"),
            ),
        )
        val script = state.toRootScript(null, emptyMap(), log)
        assertTrue(script.contains("\nid\n"))
        assertTrue(script.contains("\nuname -a\n"))
    }

    @Test
    fun safeModeIsPassedToLateLoad() {
        val withSafe = FinishState(safeMode = true).toRootScript(null, emptyMap(), log)
        assertTrue(withSafe.contains("late-load --allow-shell --safe-mode"))
        val without = FinishState().toRootScript(null, emptyMap(), log)
        assertFalse(without.contains("--safe-mode"))
    }

    @Test
    fun quotesSingleQuotes() {
        assertTrue(shQuote("a'b").contains("'\\''"))
        val script = FinishState(
            payloads = listOf(PayloadSlot("cmd", PayloadKind.Command, command = "echo a'b")),
        ).toRootScript(null, emptyMap(), log)
        assertTrue(script.contains("echo a'b"))
    }
}
