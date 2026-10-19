package com.ghostlock.app.engine.pagecache

import com.ghostlock.app.jni.cve43284.Assembled43284
import java.io.File
import java.io.FileInputStream
import java.io.InputStream

/**
 * 复原载体的执行体（纯 Kotlin：不碰 native，也不读任何设备状态）。
 *
 * # 这是我们自己的补救路径（ref 没有复原路径）
 * 参考实现 `ghostlock-app-ref@5f184760` 的终点恢复只做一件事：把钩子站点的 guard/displaced
 * 两个字写回 libc++（`src/core/backend/cve_2026_43284/steps/chain.cpp:355` 的
 * "It runs on every terminus path"，实现在同树 `steps/hook_patch.cpp:193-225`），
 * 它**从不**把被写坏的载体 ELF 头修回去 —— 那份实现的写链假定载体是厂家原件，只把它当页缓存写目标
 * （`pagecache.cpp:158-165`）。本仓这次跑链把字节写进了 `/vendor/lib64/libbinderdebug.so` 的
 * offset 0（最多 [TypedBackendProbe.RESTORE_BYTES] 字节），原厂字节未留存；唯一可复原来源是
 * `/system/lib64/libbinderdebug.so`（91088 B、world-readable，与载体同源）。所以复原逻辑只能自己写：
 * 写原语照旧用 `Assembled43284.writeImage`，读回照旧用既有 `readBlock`，判据只有两条 ——
 * 块是否全部写完、0 偏移的头四字节是否回到 `7f454c46`。
 *
 * # 与攻击路径的显式差别
 * 攻击前的两道判据（模块 precheck、计划闭包）在这里**一律不跑**：它们判的是"这份镜像能不能加载 /
 * 三个写区是否合法"，而复原是把原厂字节写回去 —— 既不加载模块，也不拼装钩子。复原只保留写原语本身。
 *
 * # 边界
 * 只用于复原，**不得用于攻击**：本类不 hook、不 trigger、不 insmod，也不产生任何新字节；
 * 它写入的每一个字节都来自调用方给的原厂来源。
 *
 * @param emit 逐行日志回调（是否并入统一日志由调用方决定）。
 */
internal class CarrierRestore(private val emit: (String) -> Unit) {

    /**
     * 复原会话需要的能力面。
     *
     * 生产实现是 `TypedBackendProbe` 里的适配器（一块 = 一次 `writeImage`）；单测用计数 fake。
     */
    interface Session {
        /**
         * 攻击路径的模块加载预检。
         *
         * @note 复原路径**绝不**调用它。把它留在接口上，是为了让"复原不调用 precheck"成为
         *       **可被单测计数证明**的约束（fake 的计数不为 0 ⇒ 测试红），而不是注释里的一句承诺。
         * @param image 模块镜像（本路径不会传）。
         * @return 预检是否通过（本路径不看）。
         */
        fun precheckModule(image: ByteArray): Boolean

        /**
         * 写一块。
         *
         * @param offset 载体内的绝对偏移（从 0 起、按 [WIDTH] 递增）。
         * @param block  [WIDTH] 字节（最后一块按原厂字节补齐）。
         * @return true = 这一块写完并校验通过；false = 具名拒绝（原因由生产实现记日志）。
         */
        fun writeBlock(offset: Long, block: ByteArray): Boolean

        /** @return 载体 offset 0 的头四字节；读不到 null。 */
        fun readHead(): ByteArray?

        /** 关掉复原会话（生产实现收尾；单测 fake 不需要）。 */
        fun close() {}
    }

    /**
     * 一块的坐标与内容。
     *
     * @property offset 载体内的绝对偏移。
     * @property bytes  送进写原语的 [WIDTH] 字节（末块用 0 补齐）。
     * @property length 这一块真正来自原厂的字节数（末块可能不足 [WIDTH]）。
     */
    class Chunk(val offset: Long, val bytes: ByteArray, val length: Int)

    /**
     * 跑一次复原：读原厂字节 → 逐块写回 → 回读头四字节 → **无条件**调用既有 restoreHook。
     *
     * 顺序（日志第一行固定是 `restore-mode: precheck bypassed (carrier restore)`）：
     *  1. 取原厂来源（失败只记一行，钩子还原照样跑）；
     *  2. 建复原会话（生产实现走 create / initialize / writeImage 那套会话）；
     *  3. 读一次载体头四字节 → 逐块写 → 再读一次 → 打 `restore: head before=… after=… (expect 7f454c46)`；
     *  4. finally：先关复原会话（让出 ESP socket 的固定 sender 端口），再**无条件**调用 `restoreHook`
     *     并打 `restore: hook restored=<bool>`。
     *
     * @param source      读原厂字节（返回 null = 没有可用的来源；异常由本方法记一行）。
     * @param session     建复原会话（返回 null = 建不起来；异常由本方法记一行）。
     * @param restoreHook 既有 restoreHook 的一次调用（本方法保证无论成败都调用它）。
     * @param totalBytes  只复原的字节数；默认 [TypedBackendProbe.RESTORE_BYTES]。
     * @return 载体是否复原成功：块全部写完、计划覆盖 [totalBytes]、0 偏移回读为 ELF magic。
     *         钩子还原的结果**不参与**这个返回值（它单独打一行）。
     */
    fun run(
        source: () -> ByteArray?,
        session: () -> Session?,
        restoreHook: () -> Boolean,
        totalBytes: Int = TypedBackendProbe.RESTORE_BYTES,
    ): Boolean {
        emit("restore-mode: precheck bypassed (carrier restore)")
        emit("restore-mode: plan gate bypassed (carrier restore)")

        val original = try {
            source()
        } catch (problem: Throwable) {
            emit("restore: source refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))
            null
        }
        if (original == null) {
            emit("restore: source unavailable path=" + SOURCE_PATH)
        } else {
            emit("restore: source path=" + SOURCE_PATH + " bytes=" + original.size + " need=" + totalBytes)
        }

        var carrierOk = false
        var handle: Session? = null
        try {
            if (original == null) {
                emit("restore: carrier not attempted (source missing)")
            } else {
                handle = try {
                    session()
                } catch (problem: Throwable) {
                    emit("restore: session refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))
                    null
                }
                carrierOk = if (handle == null) {
                    emit("restore: carrier not attempted (session unavailable)")
                    false
                } else {
                    writeCarrier(handle, original, totalBytes)
                }
            }
        } finally {
            /* 先关复原会话：它会释放 ESP socket。socket 绑的是 SA 的**固定** sender 端口
             * （native device_connect_esp 的 bind），而钩子还原会话要自己 bind 同一个端口 ——
             * 端口没让出来，第二次 bind 就是 esp-refused。关会话失败不拦钩子还原：两件事各自记账。 */
            try {
                handle?.close()
            } catch (problem: Throwable) {
                emit("restore: session close refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))
            }
            /* 终点义务：钩子还原无条件跑（成功、失败、建会话失败都一样）。 */
            val restored = try {
                restoreHook()
            } catch (problem: Throwable) {
                emit("restore: hook restore refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))
                false
            }
            emit("restore: hook restored=" + restored)
            /* 固定收尾行（本批要求，与 restore: hook restored= 成对）：
             * n = 本次复原负责的字节数（totalBytes = RESTORE_BYTES）；
             * ok = 载体是否复原。部分写回的进度见上面 restore: wrote=... 行。 */
            emit("restore: bytes=" + totalBytes + " ok=" + carrierOk)
        }
        return carrierOk
    }

    /** 逐块写回并回读头四字节；返回载体是否完好。 */
    private fun writeCarrier(session: Session, original: ByteArray, totalBytes: Int): Boolean {
        val chunks = plan(original, totalBytes)
        val planned = chunks.sumOf { chunk -> chunk.length }
        emit("restore: plan blocks=" + chunks.size + " bytes=" + planned + "/" + totalBytes + " width=" + WIDTH)
        /* 写前读一次；写原语是十六字节块，所以读回的是整块、这里只报头四字节。 */
        val before = try {
            session.readHead()
        } catch (problem: Throwable) {
            null
        }
        var wrote = 0
        var failedAt = -1L
        for (chunk in chunks) {
            val ok = try {
                session.writeBlock(chunk.offset, chunk.bytes)
            } catch (problem: Throwable) {
                emit(
                    "restore: block threw offset=" + chunk.offset + " " +
                        problem.javaClass.simpleName + ":" + (problem.message ?: ""),
                )
                false
            }
            if (!ok) {
                failedAt = chunk.offset
                break
            }
            wrote += 1
        }
        val after = try {
            session.readHead()
        } catch (problem: Throwable) {
            null
        }
        emit("restore: head before=" + headHex(before) + " after=" + headHex(after) + " (expect 7f454c46)")
        if (failedAt >= 0L) {
            emit("restore: block refused offset=" + failedAt + " wrote=" + wrote + "/" + chunks.size)
        }
        val written = chunks.take(wrote).sumOf { chunk -> chunk.length }
        emit("restore: wrote=" + wrote + "/" + chunks.size + " bytes=" + written + "/" + totalBytes)
        val complete = failedAt < 0L && wrote == chunks.size && planned == totalBytes
        return complete && hasElfMagic(after)
    }

    companion object {
        /** 原厂字节的来源：与载体同源的 system 副本（91088 B、world-readable）。 */
        const val SOURCE_PATH: String = "/system/lib64/libbinderdebug.so"

        /** 载体：这次被写坏的那份 vendor 库。 */
        const val CARRIER_PATH: String = "/vendor/lib64/libbinderdebug.so"

        /** 写原语的块宽（AES-CBC 一块；native 只接受 16）。 */
        const val WIDTH: Int = Assembled43284.BLOCK

        /** ELF 头四字节：载体复原成功的判据。 */
        private val ELF_MAGIC = byteArrayOf(0x7f, 0x45, 0x4c, 0x46)

        /**
         * 读原厂来源的前 [totalBytes] 字节。
         *
         * @param totalBytes 需要的字节数（> 0）。
         * @param path       来源路径（默认 [SOURCE_PATH]）。
         * @return 实际读到的字节；不足时是较短的**前缀**（调用方按"来源不够长"判失败）。
         * @throws java.io.IOException 文件打不开/读失败（调用方把它写成一行具名日志，不吞）。
         * @note 只读 [totalBytes]：来源整份 91088 B，但我们只可能写过前 23512 B，
         *       再往后没有证据说被改过，不能凭猜覆盖。
         */
        @Throws(java.io.IOException::class)
        fun readSource(totalBytes: Int, path: String = SOURCE_PATH): ByteArray {
            require(totalBytes > 0) { "totalBytes must be positive" }
            return FileInputStream(File(path)).use { stream -> readExactly(stream, totalBytes) }
        }

        /** 读满 [total] 字节或读到文件尾为止（不依赖 API 33 才有的 readNBytes）。 */
        private fun readExactly(stream: InputStream, total: Int): ByteArray {
            val out = ByteArray(total)
            var filled = 0
            while (filled < total) {
                val got = stream.read(out, filled, total - filled)
                if (got <= 0) {
                    break
                }
                filled += got
            }
            return if (filled == total) out else out.copyOf(filled)
        }

        /**
         * 复原块的坐标：offset 从 0 起、按 [width] 递增；末块补齐到 [width]。
         *
         * @param original   原厂来源（只取前 [totalBytes] 字节）。
         * @param totalBytes 只复原的字节数（默认 [TypedBackendProbe.RESTORE_BYTES]）。
         * @param width      块宽（默认 [WIDTH]；native 只接受 16）。
         * @return 从 0 开始的块序列；来源不足时序列只覆盖实际读到的前缀。
         */
        fun plan(
            original: ByteArray,
            totalBytes: Int = TypedBackendProbe.RESTORE_BYTES,
            width: Int = WIDTH,
        ): List<Chunk> {
            require(width > 0) { "width must be positive" }
            val usable = minOf(original.size, maxOf(totalBytes, 0))
            val out = ArrayList<Chunk>((usable + width - 1) / width)
            var offset = 0
            while (offset < usable) {
                val end = minOf(offset + width, usable)
                val slice = original.copyOfRange(offset, end)
                out += Chunk(
                    offset = offset.toLong(),
                    bytes = if (slice.size == width) slice else slice.copyOf(width),
                    length = end - offset,
                )
                offset = end
            }
            return out
        }

        /** @return 头四字节的十六进制（不足四个就按实际长度；null = 没读到）。 */
        private fun headHex(head: ByteArray?): String =
            if (head == null) "none" else head.take(4).joinToString("") { byte -> "%02x".format(byte) }

        /** @return 头四字节是否等于 ELF magic（`7f454c46`）。 */
        private fun hasElfMagic(head: ByteArray?): Boolean {
            if (head == null || head.size < ELF_MAGIC.size) {
                return false
            }
            for (index in ELF_MAGIC.indices) {
                if (head[index] != ELF_MAGIC[index]) {
                    return false
                }
            }
            return true
        }
    }
}
