package com.ghostlock.app

import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.ui.screen.module.backends.FinishExecution
import java.io.File
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Assume
import org.junit.Test
import org.junit.runner.RunWith

/**
 * 手动门禁：由 host 侧 `insmod` 一个已补丁 knob 的模块，让真实 UMH 执行 `run.sh`。
 *
 * 默认跳过；用 `-Pandroid.testInstrumentationRunnerArguments.umhGate=true` 运行。
 * 测试只负责准备脚本/日志 memfd 并等待，不自己执行脚本。
 */
@RunWith(AndroidJUnit4::class)
class UmhGateInstrumentedTest {

    @Test
    fun umhRunsPreparedScript() {
        Assume.assumeTrue(
            InstrumentationRegistry.getArguments().getString("umhGate") == "true",
        )
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val session = FinishExecution.prepare(context)
        assertNotNull(session)
        val publish = File(context.getExternalFilesDir(null), "umh-gate.path")
        publish.writeText(session!!.script.absolutePath)
        try {
            Thread.sleep(25_000)
            val log = session.logFile.readText()
            log.lineSequence().take(80).forEach { line -> Log.i("GhostlockUmhLog", line) }
            assertTrue("日志缺少 finish start: " + log, log.contains("finish start"))
            assertTrue("日志缺少 finish done: " + log, log.contains("finish done"))
        } finally {
            publish.delete()
            session.finish()
            session.logFile.delete()
        }
    }
}
