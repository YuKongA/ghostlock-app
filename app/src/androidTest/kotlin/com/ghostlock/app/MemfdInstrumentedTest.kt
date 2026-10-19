package com.ghostlock.app

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.jni.NativeMemfd
import com.ghostlock.app.ui.screen.module.backends.FinishScriptStore
import com.ghostlock.app.ui.screen.module.backends.LogMemfd
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * 设备侧最小证据：memfd 的 JNI、run.sh 生命周期、日志 drainer + fdatasync。
 *
 * 不触发攻击、不用 root；模拟器即可跑，真机门禁仍按计划单独执行。
 */
@RunWith(AndroidJUnit4::class)
class MemfdInstrumentedTest {
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext

    @Test
    fun memfdRoundTrip() {
        val fd = NativeMemfd.create()
        assertTrue("memfd_create failed: " + fd, fd >= 0)
        val data = "hello-memfd".toByteArray()
        assertEquals(data.size, NativeMemfd.write(fd, data, 0))
        val out = ByteArray(data.size + 4)
        assertEquals(data.size, NativeMemfd.read(fd, out, 0))
        assertArrayEquals(data, out.copyOf(data.size))
        assertTrue(NativeMemfd.sync(fd))
        assertTrue(NativeMemfd.close(fd))
    }

    @Test
    fun prepareRendersLogPathAndPatchesImage() {
        val session = com.ghostlock.app.ui.screen.module.backends.FinishExecution.prepare(context)
        assertNotNull(session)
        try {
            val script = session!!.script
            assertTrue(script.isFile)
            val body = script.readText()
            assertTrue("脚本缺少 memfd 通道: " + body, body.contains("LOG='/proc/"))
            assertTrue(body.contains("exec 3>>\"\$LOG\""))

            val image = com.ghostlock.app.engine.pagecache.LkmSelector.read(context)
            assertNotNull("APK 缺少当前 KMI 的 LKM 镜像", image)
            val patched = session.patcher(image!!)
            val marker = "GLKKNOB1".toByteArray()
            val at = patched.indexOfSlice(marker)
            assertTrue("镜像缺少 knob magic", at >= 0)
            val length = (patched[at + 24].toInt() and 0xFF) or
                ((patched[at + 25].toInt() and 0xFF) shl 8)
            assertTrue("路径未写入", length > 0)
            val written = String(patched.copyOfRange(at + 28, at + 28 + length))
            assertEquals(script.absolutePath, written)
        } finally {
            session?.finish()
            session?.logFile?.delete()
        }
    }

    private fun ByteArray.indexOfSlice(needle: ByteArray): Int {
        outer@ for (index in 0..(size - needle.size)) {
            for (offset in needle.indices) {
                if (this[index + offset] != needle[offset]) continue@outer
            }
            return index
        }
        return -1
    }

    /**
     * 手动门禁：把 memfd 路径发布到外部 files，等 root 侧读取。
     *
     * 默认跳过；用 `-Pandroid.testInstrumentationRunnerArguments.rootProbe=true`
     * 单独跑，再由 adb root 按发布的路径 `cat` 验证。
     */
    @Test
    fun memfdRootProbe() {
        org.junit.Assume.assumeTrue(
            androidx.test.platform.app.InstrumentationRegistry.getArguments()
                .getString("rootProbe") == "true",
        )
        val fd = NativeMemfd.create()
        assertTrue(fd >= 0)
        assertTrue(NativeMemfd.write(fd, "root-probe\n".toByteArray(), 0) > 0)
        val publish = java.io.File(context.getExternalFilesDir(null), "root-probe.path")
        publish.writeText("/proc/" + android.os.Process.myPid() + "/fd/" + fd)
        try {
            Thread.sleep(20_000)
        } finally {
            publish.delete()
            NativeMemfd.close(fd)
        }
    }

    /**
     * 手动门禁：把 `run.sh` 路径发布出去，由 adb root 执行；测试侧验证日志 memfd 收到
     * `finish start` 与 `finish done`（完整脚本 + drainer + fdatasync，不含 UMH）。
     */
    @Test
    fun scriptRootExecutionProbe() {
        org.junit.Assume.assumeTrue(
            androidx.test.platform.app.InstrumentationRegistry.getArguments()
                .getString("scriptProbe") == "true",
        )
        val session = com.ghostlock.app.ui.screen.module.backends.FinishExecution.prepare(context)
        assertNotNull(session)
        val publish = java.io.File(context.getExternalFilesDir(null), "script-probe.path")
        publish.writeText(session!!.script.absolutePath)
        try {
            Thread.sleep(20_000)
            val log = session.logFile.readText()
            assertTrue("日志缺少 finish start: " + log, log.contains("finish start"))
            assertTrue("日志缺少 finish done: " + log, log.contains("finish done"))
        } finally {
            publish.delete()
            session.finish()
            session.logFile.delete()
        }
    }

    @Test
    fun scriptFileLifecycle() {
        val file = FinishScriptStore.write(context, "#!/system/bin/sh\necho hi\n")
        assertTrue(file.isFile)
        assertTrue(file.readText().contains("echo hi"))
        FinishScriptStore.delete(context)
        assertFalse(file.exists())
    }

    @Test
    fun logMemfdDrainsToFile() {
        val log = LogMemfd.open(context, System.currentTimeMillis())
        assertNotNull(log)
        log!!.start()
        assertTrue(log.appendForTest("hello-log\n".toByteArray()))
        Thread.sleep(400)
        log.stop()
        assertTrue(log.file.isFile)
        assertTrue(log.file.readText().contains("hello-log"))
        log.file.delete()
    }
}
