package com.ghostlock.app.backend.cve43499.victim

import android.os.ParcelFileDescriptor
import android.system.Os
import android.system.OsConstants
import java.io.BufferedReader
import java.io.BufferedWriter
import java.io.File
import java.io.FileDescriptor
import java.io.FileInputStream
import java.io.FileOutputStream
import java.io.IOException
import java.security.MessageDigest
import java.util.concurrent.TimeUnit

/**
 * 最小受害者进程 seam；[close] 拥有有界终结，不泄漏 child。
 *
 * @note 阶段：G4L-6 victim；实现必须是 Kotlin 进程（ProcessBuilder），不走 JNI。
 */
internal interface VictimProcess : AutoCloseable {
    /** @return 当前 uid；读失败返回 null。实现必须有界。 */
    fun uid(): Int?

    /** @return 进程 pid；fake/不可得返回 null（identity 只读证据用）。 */
    fun pid(): Int? = null

    /**
     * 在 victim 内执行 finish 脚本协议行（`finish <s> '<path>'` 或等价）。
     *
     * @param commandLine 已由 [VictimProtocol.finishCommand] 校验/编码的协议行。
     * @return 脚本退出码+输出摘要，或具名失败；实现不得抛异常。
     * @note 默认实现返回 `victim-finish:unsupported`（宿主 fake 可只覆写需要的命令）。
     */
    fun finish(commandLine: String): VictimFinishResult =
        VictimFinishResult.Failed("victim-finish:unsupported")

    /**
     * 请求 victim 驻留（rooted child 不杀；不再读 stdin）。
     *
     * @return true=请求已发出；false=写入失败（进程可能已退出）。
     */
    fun park(): Boolean = false

    /**
     * 请求 victim 退出（非 rooted 的正常终结路径）。
     *
     * @return true=请求已发出；false=写入失败（进程可能已退出）。
     */
    fun bye(): Boolean = false

    /** 有界、幂等的终结；重复调用是无副作用 no-op。 */
    override fun close()

    /* task-86 append：公共 self-probe（process 路径 provisional 不支持 ⇒ null/false；
     * J/common helper 路径覆盖；不改变既有命令语义）。 */
    /** @return self `/proc/self/status` raw；默认不支持 null。 */
    fun status(): ByteArray? = null
    /** @param marker 15B 安全 ASCII。 @return 默认不支持 false。 */
    fun commSet(marker: String): Boolean = false
    /** @return self comm；默认不支持 null。 */
    fun comm(): Pair<Int, ByteArray>? = null
}

/**
 * task-132/S1：victim 父端 pipe 高位移交 seam（PR403）。
 *
 * @note 生产实现使用公开 `android.system.Os.fcntlInt`；单测注入 fake，不触 Android。
 */
internal fun interface VictimPipeFdRelocator {
    /**
     * @param source 父进程持有的 pipe 端。
     * @param minimum 目标 fd 下界（必须 > pselect nfds）。
     * @return 新的 fd（调用方接管所有权）；失败返回负值/errno 语义。
     */
    fun relocate(source: FileDescriptor, minimum: Int): Int
}

/** @brief 生产实现：`F_DUPFD_CLOEXEC` 到 `minimum` 以上。 */
internal object AndroidVictimPipeFdRelocator : VictimPipeFdRelocator {
    override fun relocate(source: FileDescriptor, minimum: Int): Int =
        runCatching {
            Os.fcntlInt(source, OsConstants.F_DUPFD_CLOEXEC, minimum)
        }.getOrDefault(-1)
}

/**
 * task-132/S1：从 Android `Process` 的父端 pipe 定位真实 fd。
 *
 * ProcessBuilder 返回的流由 libcore 包装（`ProcessPipeOutputStream`/`InputStream`），
 * 不公开底层 `FileDescriptor`；这里通过 `/proc/self/fd` 与子进程 `/proc/<pid>/fd/{0,1}`
 * 的 `pipe:[inode]` 符号链接匹配，再用公开 `ParcelFileDescriptor.adoptFd` 取得
 * FileDescriptor（不反射私有字段）。
 *
 * @note 任何一步不可读/匹配失败都抛 IOException，由 spawn 调用方 fail-closed。
 */
internal object AndroidProcessPipeFds {
    /** @brief 解析 `/proc` fd 符号链接的 pipe inode；非 pipe 返回 null。 */
    fun pipeInode(link: String): Long? {
        val prefix = "pipe:["
        if (!link.startsWith(prefix) || !link.endsWith("]")) return null
        return link.substring(prefix.length, link.length - 1).toLongOrNull()
    }

    /**
     * @param process 已启动子进程。
     * @return (stdin 父端, stdout 父端) 的 FileDescriptor。
     * @throws IOException 无法读取/匹配任一 pipe。
     */
    fun locate(process: Process): Pair<FileDescriptor, FileDescriptor> {
        val pid = process.pid()
        val stdinLink = readLink("/proc/$pid/fd/0")
        val stdoutLink = readLink("/proc/$pid/fd/1")
        if (pipeInode(stdinLink) == null || pipeInode(stdoutLink) == null) {
            throw IOException("victim-pipe:not-pipe")
        }
        var stdinFd = -1
        var stdoutFd = -1
        val names = File("/proc/self/fd").list() ?: throw IOException("victim-pipe:fd-list")
        for (name in names) {
            if (name.toIntOrNull() == null) continue
            val link = runCatching { readLink("/proc/self/fd/$name") }.getOrNull() ?: continue
            if (link == stdinLink && stdinFd < 0) stdinFd = name.toInt()
            if (link == stdoutLink && stdoutFd < 0) stdoutFd = name.toInt()
        }
        if (stdinFd < 0 || stdoutFd < 0 || stdinFd == stdoutFd) {
            throw IOException("victim-pipe:fd-match")
        }
        return Pair(ownedDescriptor(stdinFd), ownedDescriptor(stdoutFd))
    }

    /** @brief 读取符号链接；失败抛 IOException。 */
    private fun readLink(path: String): String =
        runCatching { Os.readlink(path) }.getOrElse {
            throw IOException("victim-pipe:readlink:" + path)
        }

    /** @brief 把已定位的裸 fd 包成 FileDescriptor；PFD detach，调用方接管 fd。 */
    private fun ownedDescriptor(fd: Int): FileDescriptor {
        val pfd = ParcelFileDescriptor.adoptFd(fd)
        val descriptor = pfd.fileDescriptor
        pfd.detachFd()
        return descriptor
    }
}

/**
 * task-132/S1：victim pipe 高位策略。
 *
 * @note 下界 = pselect nfds 320 + 64；低于下界视为失败（fail-closed），
 *       不允许 pselect 的 0..319 dup2 扫描触碰 victim pipe。
 */
internal object VictimPipeHighFd {
    /** @brief 当前 pselect descriptor capacity（5.10 profile 320）。 */
    const val PSELECT_NFDS = 320

    /** @brief 父端 pipe 最小 fd（320+64）。 */
    const val MIN_FD = PSELECT_NFDS + 64

    /**
     * @param relocator 可注入 seam。
     * @param source 原 pipe fd。
     * @return >= [MIN_FD] 的新 fd。
     * @throws IOException 返回负值/低于下界（spawn 必须 fail-closed）。
     */
    fun relocateOrThrow(relocator: VictimPipeFdRelocator, source: FileDescriptor): Int {
        val high = relocator.relocate(source, MIN_FD)
        if (high < MIN_FD) {
            throw IOException("victim-pipe:high-fd:" + high)
        }
        return high
    }
}

/**
 * Kotlin 受害者子进程：固定 shell 行协议循环（R3f/task-45）。
 *
 * 协议见 [VictimProtocol]：`probe` ⇒ `id -u`；`finish <s> '<path>'` ⇒ victim 内
 * fork/exec 脚本（有界 watchdog + 退出码/输出摘要）；`park` ⇒ 驻留不杀；`bye` ⇒ 退出。
 *
 * 构造是原子的：任一 stream 创建失败会先把已启动的进程按有界顺序收掉，再把异常抛回。
 *
 * @param command 子进程命令；默认 [DEFAULT_COMMAND]（固定字符串，不接受调用方拼接）。
 * @param streamFactory stream 构造 seam（宿主验证可在进程创建后注入失败）。
 * @note 阶段：G4L-6 victim；不读 profile、不连 Native。
 */
internal class ProcessVictim(
    private val command: List<String> = DEFAULT_COMMAND,
    streamFactory: (Process) -> Streams = { process ->
        Streams(process.outputStream.bufferedWriter(), process.inputStream.bufferedReader())
    },
    /**
     * task-132/S1：父端 pipe 高位移交；null=不移动（仅宿主 fake/测试使用）。
     * 生产默认使用 [AndroidVictimPipeFdRelocator]。
     */
    private val pipeRelocator: VictimPipeFdRelocator? = AndroidVictimPipeFdRelocator,
    /**
     * @param pipeFdLocator 父端 fd 定位 seam；默认 `/proc` 实现，宿主测试可注入。
     */
    private val pipeFdLocator: (Process) -> Pair<FileDescriptor, FileDescriptor> =
        { process -> AndroidProcessPipeFds.locate(process) },
) : VictimProcess {
    /** 与 victim 子进程交互的两条流。 */
    data class Streams(val input: BufferedWriter, val output: BufferedReader)

    private val process: Process
    private val input: BufferedWriter
    private val output: BufferedReader

    init {
        val started = ProcessBuilder(command).redirectErrorStream(true).start()
        try {
            val streams = if (pipeRelocator == null) {
                streamFactory(started)
            } else {
                relocateStreams(started, pipeRelocator)
            }
            process = started
            input = streams.input
            output = streams.output
        } catch (problem: Throwable) {
            started.destroy()
            if (!started.waitFor(CLOSE_TIMEOUT_MS, TimeUnit.MILLISECONDS)) {
                started.destroyForcibly()
            }
            throw problem
        }
    }

    /**
     * task-132/S1：把 ProcessBuilder 的父端 stdin/stdout fd 用
     * `F_DUPFD_CLOEXEC` 移到 pselect nfds+64 以上，并用高 fd 重建流。
     *
     * @param started 已启动子进程（父端 fd 由 Process 持有）。
     * @param relocator 注入 seam。
     * @return 高 fd 上的协议流。
     * @throws IOException 任一 pipe 移动/建流失败；失败时关闭已建高 fd 并抛回，
     *   调用方按 spawn 失败收掉子进程（fail-closed）。
     * @note 成功后关闭原低 fd 流，pselect 的 0..319 扫描不会再触碰 victim pipe。
     */
    private fun relocateStreams(
        started: Process,
        relocator: VictimPipeFdRelocator,
    ): Streams {
        var highInputFd = -1
        var highOutputFd = -1
        var relocatedInput: BufferedWriter? = null
        var relocatedOutput: BufferedReader? = null
        try {
            val parentFds = pipeFdLocator(started)
            highInputFd = VictimPipeHighFd.relocateOrThrow(relocator, parentFds.first)
            highOutputFd = VictimPipeHighFd.relocateOrThrow(relocator, parentFds.second)
            relocatedInput = FileOutputStream(
                ownedFileDescriptor(highInputFd),
            ).bufferedWriter()
            highInputFd = -1 /* 所有权已交给 relocatedInput */
            relocatedOutput = FileInputStream(
                ownedFileDescriptor(highOutputFd),
            ).bufferedReader()
            highOutputFd = -1 /* 所有权已交给 relocatedOutput */
            /* 高 fd 已建立；原低 fd 由旧流拥有，关闭后低 fd 不再被 victim 使用。 */
            runCatching { started.outputStream.close() }
            runCatching { started.inputStream.close() }
            return Streams(
                requireNotNull(relocatedInput) { "victim-pipe:input" },
                requireNotNull(relocatedOutput) { "victim-pipe:output" },
            )
        } catch (problem: Throwable) {
            closeOwnedFd(highInputFd)
            closeOwnedFd(highOutputFd)
            runCatching { relocatedInput?.close() }
            runCatching { relocatedOutput?.close() }
            runCatching { started.outputStream.close() }
            runCatching { started.inputStream.close() }
            throw problem
        }
    }

    /** @brief 把已接管所有权的 fd 包成 FileDescriptor，PFD 本身 detach 防双关。 */
    private fun ownedFileDescriptor(fd: Int): FileDescriptor {
        val pfd = ParcelFileDescriptor.adoptFd(fd)
        val descriptor = pfd.fileDescriptor
        pfd.detachFd()
        return descriptor
    }

    /** @brief 关闭尚未包成流的裸 fd；已关闭/无效时为 no-op。 */
    private fun closeOwnedFd(fd: Int) {
        if (fd >= 0) runCatching { ParcelFileDescriptor.adoptFd(fd).close() }
    }

    /**
     * 发送一次探测并读取一行 uid。
     *
     * @return uid；I/O 失败返回 null。
     * @note 与 [close] 用同一把对象锁串行，避免探测与关闭竞争同一对流。
     */
    /** @return 子进程 PID；用于 victim 身份绑定与 starttime 复查。 */
    override fun pid(): Int = process.pid().toInt()

    @Synchronized
    override fun uid(): Int? {
        if (!writeLine(VictimProtocol.COMMAND_PROBE)) return null
        return try {
            output.readLine()?.trim()?.toIntOrNull()
        } catch (failed: IOException) {
            null
        }
    }

    /**
     * G04 append：读 victim self raw `/proc/self/status`。
     *
     * @return raw 字节；不可读/格式非法 null（调用方 FAIL，不猜）。
     */
    @Synchronized
    override fun status(): ByteArray? {
        if (!writeLine(VictimProtocol.statusCommand())) return null
        return try {
            VictimProtocol.parseStatusLine(output.readLine() ?: return null)
        } catch (failed: IOException) {
            null
        }
    }

    /**
     * G04 append：写 self comm marker。
     *
     * @param marker ≤15B 安全 ASCII。 @return true=OK。
     */
    @Synchronized
    override fun commSet(marker: String): Boolean {
        val command = VictimProtocol.commSetCommand(marker) ?: return false
        if (!writeLine(command)) return false
        return try {
            VictimProtocol.parseCommSetLine(output.readLine() ?: return false)
        } catch (failed: IOException) {
            false
        }
    }

    /**
     * G04 append：读 self comm。
     *
     * @return len/bytes；不可读 null。
     */
    @Synchronized
    override fun comm(): Pair<Int, ByteArray>? {
        if (!writeLine(VictimProtocol.commCommand())) return null
        return try {
            VictimProtocol.parseCommLine(output.readLine() ?: return null)
        } catch (failed: IOException) {
            null
        }
    }

    /**
     * 在 victim 内执行 finish 脚本（R3f）。
     *
     * @param commandLine [VictimProtocol.finishCommand] 生成的协议行。
     * @return 退出码+输出 SHA-256；管道断开/读失败返回具名失败。
     * @note 输出摘要 = 脚本输出逐行（含行尾 `\n`）的 SHA-256；有界等待到期时
     *   victim 内 watchdog 会先打 [VictimProtocol.TIMEOUT_MARKER] 再 kill 脚本。
     */
    @Synchronized
    override fun finish(commandLine: String): VictimFinishResult {
        if (!writeLine(commandLine)) {
            return VictimFinishResult.Failed("victim-finish:epipe")
        }
        val digest = MessageDigest.getInstance("SHA-256")
        var timedOut = false
        var result: VictimFinishResult? = null
        while (result == null) {
            val line = try {
                output.readLine()
            } catch (failed: IOException) {
                null
            } ?: return VictimFinishResult.Failed("victim-finish:epipe")
            when {
                VictimProtocol.isTimeoutMarker(line) -> timedOut = true
                else -> {
                    val exitCode = VictimProtocol.parseExitMarker(line)
                    result = if (exitCode != null) {
                        VictimFinishResult.Ok(
                            exitCode = exitCode,
                            outputSha256 = VictimProtocol.sha256Hex(digest.digest()),
                            timedOut = timedOut,
                        )
                    } else {
                        VictimProtocol.updateDigest(digest, line)
                        null
                    }
                }
            }
        }
        return result ?: VictimFinishResult.Failed("victim-finish:epipe")
    }

    override fun park(): Boolean = writeLine(VictimProtocol.COMMAND_PARK)

    override fun bye(): Boolean = writeLine(VictimProtocol.COMMAND_BYE)

    /**
     * 写一行协议命令并 flush。
     *
     * @param line 协议行。
     * @return true=写入成功；false=I/O 失败（不抛异常）。
     */
    private fun writeLine(line: String): Boolean = try {
        input.write(line)
        input.write("\n")
        input.flush()
        true
    } catch (failed: IOException) {
        false
    }

    /** 有界、幂等地关闭流并回收子进程；忽略已退出/destroy 抛出的异常。 */
    @Synchronized
    override fun close() {
        runCatching { input.close() }
        runCatching { output.close() }
        runCatching { process.destroy() }
        try {
            if (!process.waitFor(CLOSE_TIMEOUT_MS, TimeUnit.MILLISECONDS)) {
                runCatching { process.destroyForcibly() }
                process.waitFor(CLOSE_TIMEOUT_MS, TimeUnit.MILLISECONDS)
            }
        } catch (interrupted: InterruptedException) {
            Thread.currentThread().interrupt()
        }
    }

    companion object {
        /**
         * 默认 victim 命令：固定 shell 行协议循环（不接受调用方拼接）。
         *
         * 协议实现见 [VictimProtocol]；脚本路径由 Kotlin 侧校验并单引号包裹，
         * victim shell 只做 `'...'` 去引号 + `/system/bin/sh` 执行。
         * `park` 用 `exec sleep` 让 shell 停止读 stdin 但保持进程驻留（rooted child
         * 不杀）；`bye` 正常退出。
         */
        internal val DEFAULT_COMMAND: List<String> = listOf(
            "/system/bin/sh",
            "-c",
            """
            while IFS= read -r line; do
              case "${'$'}line" in
                probe) id -u ;;
                finish\ *)
                  rest=${'$'}{line#finish }
                  timeout_s=${'$'}{rest%% *}
                  script=${'$'}{rest#* }
                  case "${'$'}script" in
                    \'*\') script=${'$'}{script#\'}; script=${'$'}{script%\'} ;;
                  esac
                  /system/bin/sh "${'$'}script" 2>&1 & child=${'$'}!
                  ( sleep "${'$'}timeout_s"; echo "${VictimProtocol.TIMEOUT_MARKER}"; kill -9 "${'$'}child" 2>/dev/null ) & watchdog=${'$'}!
                  wait "${'$'}child"; code=${'$'}?
                  kill "${'$'}watchdog" 2>/dev/null
                  wait "${'$'}watchdog" 2>/dev/null
                  echo "${VictimProtocol.EXIT_MARKER_PREFIX}${'$'}code"
                  ;;
                park) exec sleep 2147483647 ;;
                bye) exit 0 ;;
              esac
            done
            """.trimIndent(),
        )

        /** 关闭/强杀的有界等待（毫秒）。 */
        internal const val CLOSE_TIMEOUT_MS: Long = 500L
    }
}

/**
 * 生产形状的 uid 读回口：uid 来自 Kotlin 子进程，不来自 JNI。
 *
 * @param victim 已启动的受害者进程（调用方拥有其生命周期）。
 * @note 阶段：G4L-6 victim。
 */
internal class ProcessVictimProbe(private val victim: ProcessVictim) : VictimProbe {
    override val kind: VictimProbeKind = VictimProbeKind.PRODUCTION

    override fun uid(): VictimUidResult {
        val value = victim.uid() ?: return VictimUidResult.Failed("victim-probe:epipe")
        return VictimUidResult.Ok(value.toLong())
    }
}
