package com.ghostlock.app

import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.ui.screen.run.RunCoordinator
import java.io.File
import java.util.Collections
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.Assert.assertTrue
import org.junit.Assume
import org.junit.Test
import org.junit.runner.RunWith

/**
 * 手动门禁：在设备上直接跑一次完整攻击链。
 *
 * 默认跳过；用 `-Pandroid.testInstrumentationRunnerArguments.attackSmoke=true` 单独跑。
 * 事件与脚本日志都写 logcat（tag `GhostlockSmoke` / `GhostlockSmokeLog`），供 host 侧取证。
 */
@RunWith(AndroidJUnit4::class)
class AttackSmokeInstrumentedTest {

    @Test
    fun runAttackOnDevice() {
        Assume.assumeTrue(
            InstrumentationRegistry.getArguments().getString("attackSmoke") == "true",
        )
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val events = Collections.synchronizedList(mutableListOf<String>())
        val done = CountDownLatch(1)
        Thread({
            try {
                RunCoordinator.run(context) { _, level, line ->
                    events += level.name + " " + line
                }
            } catch (failure: Throwable) {
                events += "EXCEPTION " + failure.javaClass.simpleName + ": " + failure.message
            } finally {
                done.countDown()
            }
        }, "attack-smoke").start()

        assertTrue("attack timeout", done.await(180, TimeUnit.SECONDS))
        events.forEach { line -> Log.i("GhostlockSmoke", line) }
        File(context.filesDir, "finish").listFiles()
            ?.filter { it.name.startsWith("run-") && it.name.endsWith(".log") }
            ?.forEach { file ->
                file.readLines().take(400).forEach { line -> Log.i("GhostlockSmokeLog", line) }
            }
    }
}
