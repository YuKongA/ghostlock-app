package com.ghostlock.app.ui.screen.module.backends

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** 整脚本渲染：载荷顺序、收尾语句、LOG 指向 memfd、路径转义。 */
class ScriptRenderTest {

    private val log = "/proc/4242/fd/9"

    @Test
    fun rendersWholeScriptWithLogChannel() {
        val script = FinishState().toRootScript(null, null, null, log)
        assertTrue(script.startsWith("#!/system/bin/sh"))
        assertTrue(script.contains("LOG='" + log + "'"))
        assertTrue(script.contains("exec 3>>\"\$LOG\""))
        assertTrue(script.contains("log 'finish start'"))
        assertTrue(script.contains("log 'finish done'"))
        assertTrue("载荷与收尾同段", script.indexOf("finish start") < script.indexOf("finish done"))
    }

    @Test
    fun finishSwitchesAreScriptStatements() {
        val quiet = FinishState().toRootScript(null, null, null, log)
        assertFalse(quiet.contains("setenforce 1"))
        assertFalse(quiet.contains("ctl.restart zygote"))

        val loud = FinishState(restoreSelinux = true, softRebootAfterFinish = true)
            .toRootScript(null, null, null, log)
        assertTrue(loud.contains("setenforce 1"))
        assertTrue(loud.contains("setprop ctl.restart zygote"))
        assertTrue("收尾在载荷之后", loud.indexOf("setenforce 1") > loud.indexOf("finish start"))
    }

    @Test
    fun payloadOrderFollowsStateOrder() {
        val state = FinishState(
            rootManagerEnabled = true,
            runAppEnabled = true,
            appPackage = "com.example.app",
            order = listOf("app", "root"),
        )
        val script = state.toRootScript(null, null, null, log)
        val app = script.indexOf("launch app: com.example.app")
        val root = script.indexOf("start root manager")
        assertTrue("app 在 root 之前", app in 0 until root)
    }

    @Test
    fun quotesSingleQuotes() {
        assertTrue(shQuote("a'b").contains("'\\''"))
        val script = FinishState(runAppEnabled = true, appPackage = "a'b")
            .toRootScript(null, null, null, log)
        assertTrue(script.contains("a'b"))
    }
}
