package com.ghostlock.app.log

import android.os.ParcelFileDescriptor
import android.system.Os

/**
 * 日志文件：**应用打开的那一个 fd**，Kotlin 行与 native 行写进同一份。
 *
 * 为什么是 fd 而不是两处各写一份：用户裁定（2026-10-16）「屏幕上的日志必须与 adb 的一样」。
 * adb 看的是 logcat（native 的 __android_log_print 与 Kotlin 的 Log.* 都进它），屏幕看的是这里；
 * 只有两边写同一个去处，屏幕才可能与 adb 相同。native 侧由 gl_log.hpp 按格式写同一 fd（注册见
 * [NativeLogBridge]），Kotlin 侧就是本对象。
 *
 * 分工（用户裁定）：**路径、轮转、开关都在这里**（Kotlin），native 只按格式写一次并 flush。
 *
 * @note 追加写（O_APPEND）+ 一次 write(2) 写完一行：读者（adb 的 tail 或文件管理器）立刻可见，不需要 fsync。
 */
internal object LogFile {
    /*
     * 候选路径按应用一定可写的顺序排：
     *   ① 应用私有 files 目录，唯一稳定可写，且能用
     *      `adb shell run-as com.ghostlock.app cat files/ghostlock-run.log` 读回来（本包 debuggable）；
     *   ② 应用专属外部目录 /sdcard/Android/data/<pkg>/files，adb 可直接读，作为回退。
     * /data/local/tmp 不在此列：那里属于 shell，untrusted_app 域写不进去（真机已验）。
     */
    val CANDIDATES = arrayOf(
        "/data/user/0/com.ghostlock.app/files/ghostlock-run.log",
        "/data/data/com.ghostlock.app/files/ghostlock-run.log",
        "/sdcard/Android/data/com.ghostlock.app/files/ghostlock-run.log",
    )

    /** 超过它就从头重来（轮转由 Kotlin 决定，native 不参与）。 */
    const val MAX_BYTES = 2L * 1024L * 1024L

    /* 打开方式：只写 + 建 + **追加**（O_APPEND ⇒ 与 native 的 write(2) 同一条语义，谁写都不覆盖谁）。 */
    private val OPEN_MODE =
        ParcelFileDescriptor.MODE_WRITE_ONLY or ParcelFileDescriptor.MODE_CREATE or ParcelFileDescriptor.MODE_APPEND

    private val lock = Any()
    private var handle: Handle? = null

    private class Handle(val path: String, val desk: ParcelFileDescriptor) {
        val fd: Int get() = desk.fd
    }

    /** 当前打开的路径（未打开时为 null）。 */
    val path: String? get() = synchronized(lock) { handle?.path }

    /** 当前打开的 fd（未打开时为 -1）。 */
    val fd: Int get() = synchronized(lock) { handle?.fd ?: -1 }

    /** 是否已经拿到可写的 fd（没有它就退回内存环显示）。 */
    val opened: Boolean get() = fd >= 0

    /**
     * 打开（或复用已打开的）追加写 fd。
     *
     * @return 打开的路径；三个候选都不可写时为 null（调用方退回内存环）。
     */
    fun open(): String? {
        synchronized(lock) {
            handle?.let { return it.path }
            for (candidate in CANDIDATES) {
                try {
                    val file = java.io.File(candidate)
                    file.parentFile?.mkdirs()
                    /* ParcelFileDescriptor 持有这个 fd：它活多久，native 那边的注册就有效多久。 */
                    handle = Handle(candidate, ParcelFileDescriptor.open(file, OPEN_MODE))
                    return candidate
                } catch (_: Throwable) {
                    /* 换下一个候选；全部失败就当日志文件不存在（不拖累主流程）。 */
                }
            }
            return null
        }
    }

    /**
     * 追加一行（fd 可用），必要时先把超大文件轮转掉。
     *
     * @param text 整行内容（不含换行；换行由这里补）。
     * @return true 表示这一行已经交给内核；false 表示没有 fd（调用方决定怎么兜底）。
     */
    fun append(text: String): Boolean {
        synchronized(lock) {
            val current = handle ?: return false
            return try {
                /* 大小必须**现取**：ParcelFileDescriptor.getStatSize() 是打开那一刻的快照，
                 * 用它判断增长永远是 0（2026-10-16 真机实测：文件涨到 4.3 MB 也没轮转）。 */
                /*
                 * 曾经在这里 ftruncate(0)：日志一过 2 MB 就被清空，一轮链跑完只剩尾部几十行 ——
                 * 现场证据全丢（真机实测：≥4 MB ⇒ 只剩 79 行）。现在**不再清空**；
                 * 面板只读尾部窗口（200 行），文件增长不影响使用，但证据完整保留。
                 */
                /*
                 * 这里过去是 `if (size > MAX_BYTES) ftruncate(fd, 0)`：日志一过 2 MB 就被清空，
                 * 一轮链跑完只剩尾部几十行（真机实测 ≥4 MB ⇒ 只剩 79 行），现场证据全丢。
                 * 现在**不再清空**：面板只读尾部窗口，文件增长不影响使用，但证据完整保留。
                 */
                // 不再清空（见上）；MAX_BYTES 仅作为历史阈值保留在常量里。
                val bytes = (text + "\n").toByteArray(Charsets.UTF_8)
                var done = 0
                while (done < bytes.size) {
                    val wrote = Os.write(current.desk.fileDescriptor, bytes, done, bytes.size - done)
                    if (wrote <= 0) {
                        break
                    }
                    done += wrote
                }
                done == bytes.size
            } catch (_: Throwable) {
                false
            }
        }
    }

    /**
     *  追加一行 Kotlin 日志（老逻辑：首次写时挑候选路径，写失败静默）。
     *
     *  行形状与 native 侧一致：`HH:mm:ss.mmm K <级别字母>/<标签>: <正文>`。
     *
     *  @param line 已格式化的级别 + 标签 + 正文（[log] 给的 line）。
     *  @return true 表示这一行已经交给内核。
     */
    fun appendLine(line: String): Boolean {
        if (!opened) {
            open()
        }
        return append(line)
    }

    /** 从零重来（清屏按键）：轮转的第二种触发方式，仍由 Kotlin 决定。 */
    fun truncate() {
        synchronized(lock) {
            val current = handle ?: return
            try {
                Os.ftruncate(current.desk.fileDescriptor, 0L)
            } catch (_: Throwable) {
                /* 截不动就保持原样：清屏失败不是主流程的事。 */
            }
        }
    }

}
