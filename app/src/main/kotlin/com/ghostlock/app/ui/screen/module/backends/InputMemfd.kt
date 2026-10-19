package com.ghostlock.app.ui.screen.module.backends

import android.content.Context
import android.net.Uri
import android.os.Process
import com.ghostlock.app.jni.NativeMemfd

/**
 * 用户选择或 APK 内资源的只读输入：落进 memfd，root 通过 `/proc/<pid>/fd/<n>` 读取。
 *
 * 输入例外，不是通信通道：脚本与 FolkPatch 的 ko 只在这里过界，用完即关。
 */
internal class InputMemfd private constructor(val fd: Int) {
    /** 脚本里的路径：UMH 以 root 打开这个路径。 */
    val path: String = "/proc/" + Process.myPid() + "/fd/" + fd

    /** 关闭 fd；重复调用无副作用。 */
    fun close() {
        NativeMemfd.close(fd)
    }

    companion object {
        /** 把字节写进 memfd；空数组或写不全返回 null。 */
        fun fromBytes(bytes: ByteArray): InputMemfd? {
            if (bytes.isEmpty()) return null
            val fd = NativeMemfd.create()
            if (fd < 0) return null
            val written = NativeMemfd.write(fd, bytes, 0)
            if (written != bytes.size) {
                NativeMemfd.close(fd)
                return null
            }
            return InputMemfd(fd)
        }

        /** 读 `content://` 等无路径 URI 的全部字节；失败返回 null。 */
        fun fromUri(context: Context, uri: String): InputMemfd? = runCatching {
            context.contentResolver.openInputStream(Uri.parse(uri))?.use { it.readBytes() }
        }.getOrNull()?.let(::fromBytes)
    }
}
