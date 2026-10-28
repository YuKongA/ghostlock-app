package com.ghostlock.app.backend.cve98260.payload

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 98260 启动脚本内容守卫（backend-owned；由公用 `VictimScriptLauncher` 执行）。
 */
class Cve98260LaunchScriptTest {
    private fun script(): String = Cve98260LaunchScript.render(
        payloadPath = "/data/local/tmp/glk98260-helper",
        params = "1,1000,2",
        finishScript = "/data/local/tmp/glk98260-finish.sh",
        runId = "run-12345678",
        logPath = "/data/local/tmp/glk98260.log",
        pidPath = "/data/local/tmp/glk98260.pid",
    )

    @Test
    fun scriptBackgroundsPayloadAndReturnsPid() {
        val script = script()
        assertTrue(script.contains("--mode fake"))
        assertTrue(script.contains("--finish-script '/data/local/tmp/glk98260-finish.sh'"))
        assertTrue(script.contains("--run-id 'run-12345678'"))
        assertTrue(script.contains("</dev/null"))
        assertTrue(script.contains("&"))
        assertTrue(script.contains("echo $! > '/data/local/tmp/glk98260.pid'"))
        assertTrue(script.contains("exit 0"))
    }

    @Test
    fun scriptDoesNotKillOrWaitPayload() {
        val script = script()
        assertFalse(script.contains("kill "))
        assertFalse(script.contains("wait"))
        assertFalse(script.contains("timeout "))
    }
}
