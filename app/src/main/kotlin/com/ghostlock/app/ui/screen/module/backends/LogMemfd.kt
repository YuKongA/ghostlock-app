package com.ghostlock.app.ui.screen.module.backends

import android.content.Context
import android.os.Process
import android.system.Os
import com.ghostlock.app.jni.NativeMemfd
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileOutputStream
import java.util.concurrent.atomic.AtomicBoolean

/**
 * root 侧日志的匿名通道：脚本写 memfd，App 并行落到 `run-<epoch>.log`。
 *
 * 生命周期：`open` → `start` → 攻击期间每 50 ms drain 一次 → `stop` 做最后一次
 * drain、flush 半行、`fdatasync`。日志文件不设上限、不截断；panic 时已同步的行留在盘上。
 */
class LogMemfd private constructor(
    private val fd: Int,
    /** 本次 run 的日志文件；文件名按开始时间生成，互不覆盖。 */
    val file: File,
) {
    /** 脚本里的 `LOG=` 取值。 */
    val path: String = "/proc/" + Process.myPid() + "/fd/" + fd

    private val running = AtomicBoolean(false)
    private val lock = Any()
    private val assembler = LineAssembler()
    private var offset = 0L
    private var stream: FileOutputStream? = null
    private var thread: Thread? = null

    /** 最近一次 I/O 失败原因；成功路径恒为 null。 */
    @Volatile
    var lastError: String? = null
        private set

    /** 仅仪器测试用：模拟 root 侧追加；生产路径由脚本写 `/proc/<pid>/fd/<n>`。 */
    private var testWritten = 0L

    internal fun appendForTest(bytes: ByteArray): Boolean {
        val written = NativeMemfd.write(fd, bytes, testWritten)
        if (written != bytes.size) return false
        testWritten += written
        return true
    }

    /** 启动 drainer；重复调用无副作用。 */
    fun start() {
        if (!running.compareAndSet(false, true)) return
        stream = runCatching { FileOutputStream(file, true) }.getOrNull()
        if (stream == null) {
            lastError = "log-open-failed"
            running.set(false)
            return
        }
        thread = Thread({ loop() }, "glk-log-drain").apply {
            isDaemon = true
            start()
        }
    }

    /**
     * 停止 drainer 并做最后同步。
     *
     * 先停线程再 drain 一次，保证 `stop` 返回后文件包含 memfd 的全部内容。
     */
    fun stop() {
        running.set(false)
        thread?.let { runCatching { it.join(2_000) } }
        thread = null
        drainAll()
        synchronized(lock) {
            assembler.flush()?.let { write(it) }
            runCatching { stream?.fd?.let { descriptor -> Os.fdatasync(descriptor) } }
            runCatching { stream?.close() }
            stream = null
        }
        NativeMemfd.close(fd)
    }

    private fun loop() {
        while (running.get()) {
            drainAll()
            runCatching { Thread.sleep(50L) }
        }
    }

    /** 把 memfd 当前可读的字节全部搬走；可被线程与 stop 并发调用。 */
    private fun drainAll() {
        val buffer = ByteArray(8 * 1024)
        while (true) {
            val read = NativeMemfd.read(fd, buffer, offset)
            if (read <= 0) {
                if (read < 0) lastError = "log-read-" + read
                return
            }
            offset += read
            synchronized(lock) {
                assembler.feed(buffer, read).forEach { line -> write(line) }
                runCatching { stream?.fd?.let { descriptor -> Os.fdatasync(descriptor) } }
            }
        }
    }

    private fun write(line: ByteArray) {
        val target = stream ?: return
        val terminated = line.isNotEmpty() && line[line.size - 1] == '\n'.code.toByte()
        runCatching {
            target.write(line)
            if (!terminated) target.write(byteArrayOf('\n'.code.toByte()))
            target.flush()
        }.onFailure { lastError = "log-write-" + (it.javaClass.simpleName) }
    }

    companion object {
        /**
         * 创建 memfd 与日志文件。
         *
         * @param startedAt 本次 run 的开始毫秒数，用于文件名。
         * @return null 表示 memfd 或文件创建失败，调用方按 `prep-failed` 处理。
         */
        fun open(context: Context, startedAt: Long): LogMemfd? {
            val fd = NativeMemfd.create()
            if (fd < 0) return null
            val file = logFile(context, startedAt)
            val parent = file.parentFile
            if (parent != null && !parent.isDirectory && !parent.mkdirs()) {
                NativeMemfd.close(fd)
                return null
            }
            return LogMemfd(fd, file)
        }

        /** 本次 run 的日志文件；只生成路径，不创建。 */
        fun logFile(context: Context, startedAt: Long): File =
            File(File(context.filesDir, "finish"), "run-" + startedAt + ".log")
    }
}

/**
 * 行重组：把任意分片的字节拼成完整行。
 *
 * 只按 `0x0A` 切分，兼容 UTF-8 多字节；残片留在内部，由 [flush] 交出。
 */
internal class LineAssembler {
    private val pending = ByteArrayOutputStream()

    /** 吃进一段字节，返回所有已完整的行（含换行符）。 */
    fun feed(bytes: ByteArray, length: Int): List<ByteArray> {
        val lines = ArrayList<ByteArray>()
        var start = 0
        for (index in 0 until length) {
            if (bytes[index] == '\n'.code.toByte()) {
                pending.write(bytes, start, index - start + 1)
                lines += pending.toByteArray()
                pending.reset()
                start = index + 1
            }
        }
        if (start < length) pending.write(bytes, start, length - start)
        return lines
    }

    /** 交出未闭合的残片（不带换行）；没有残片返回 null。 */
    fun flush(): ByteArray? {
        if (pending.size() == 0) return null
        return pending.toByteArray().also { pending.reset() }
    }
}
