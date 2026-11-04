package com.ghostlock.app

import android.app.Application
import com.ghostlock.app.log.LogFile
import com.ghostlock.app.log.NativeLogBridge
import com.ghostlock.app.log.log

/**
 *  进程入口（manifest 的 android:name=".GhostLockApp" 指向本类）。
 *
 *  职责只有一件：**先把日志文件打开**，再让任何业务跑起来（2026-10-17 用户裁定：不能只依赖 logcat）。
 *  fd 打开后立刻交给 native 三个库（[NativeLogBridge]），native 行与 Kotlin 行写进同一份文件；
 *  屏幕上的弹窗（LogPopup）读的就是这份文件的尾部。
 */
class GhostLockApp : Application() {
    /** Application-scoped dependency composition root initialized before UI construction. */
    lateinit var container: AppContainer
        private set

    override fun onCreate() {
        super.onCreate()
        container = AppContainer(this)
        val opened = LogFile.open()
        if (opened == null) {
            log.warn("app: 日志文件不可写（只走 logcat + 内存环）")
            return
        }
        log.info(NativeLogBridge.attach(LogFile.fd) + " path=" + opened)
    }
}
