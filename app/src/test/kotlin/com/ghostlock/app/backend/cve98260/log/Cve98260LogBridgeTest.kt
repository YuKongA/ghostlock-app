package com.ghostlock.app.backend.cve98260.log

import com.ghostlock.app.log.LogLevel
import com.ghostlock.app.log.LogSink
import com.ghostlock.app.log.log
import java.io.File
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * R1b 单测：app 日志 fd 桥的静态契约 + SHELL fallback 行格式。
 *
 * @note 纯 JVM；fd 跨 uid/binder 行为属 G14 真机门禁（pending）。
 */
class Cve98260LogBridgeTest {
    private class RecordingSink : LogSink {
        val lines = mutableListOf<String>()
        override fun write(level: LogLevel, tag: String, message: String, line: String) {
            lines += line
        }
    }

    @After
    fun restoreLogSinks() {
        log.install()
    }

    @Test
    fun shellFallbackUsesShellOriginAndSingleLine() {
        val sink = RecordingSink()
        log.install(sink)
        assertTrue(log.shell("payload line"))
        assertEquals(1, sink.lines.size)
        assertTrue(sink.lines.first().startsWith("(*)"))
        assertTrue(sink.lines.first().endsWith("payload line"))
        assertFalse(sink.lines.first().contains("\n"))
    }

    @Test
    fun aidlExposesAttachLogMethod11() {
        val aidl = readRepoFile("app/src/main/aidl/com/ghostlock/app/shizuku/IGhostlockUserService.aidl")
        assertTrue(aidl.contains("void attachLog(in ParcelFileDescriptor logFd) = 11;"))
        assertTrue(aidl.contains("import android.os.ParcelFileDescriptor;"))
        assertTrue(aidl.contains("int prepare(String backendId, in Bundle config, in byte[] payload) = 3;"))
    }

    @Test
    fun jniBridgeWritesViaGlLogAndOwnsRawFd() {
        val source = readRepoFile("app/src/main/cpp/gl98260/jni98260.c")
        assertTrue(source.contains("common/gl_log.h"))
        assertTrue(source.contains("gl_log_attach_fd"))
        assertTrue(source.contains("gl_log_write"))
        assertTrue("JNI must close the detached raw fd", source.contains("close((int)fd)"))
        val code = source
            .replace(Regex("/\\*.*?\\*/", RegexOption.DOT_MATCHES_ALL), "")
            .replace(Regex("//[^\\n]*"), "")
        assertFalse(code.contains("kill("))
    }

    @Test
    fun clientPassesDupAndResetsOnDisconnect() {
        val source = readRepoFile(
            "app/src/main/kotlin/com/ghostlock/app/shizuku/ShizukuBackendClient.kt",
        )
        assertTrue(source.contains("ParcelFileDescriptor.dup(source)"))
        assertTrue(source.contains("binder.attachLog(descriptor)"))
        assertTrue(source.contains("logAttached = false"))
    }

    private fun readRepoFile(relative: String): String {
        val candidates = listOf(File(relative), File("../$relative"), File("../../$relative"))
        return candidates.firstOrNull { it.isFile }?.readText()
            ?: error("not found: $relative")
    }
}
