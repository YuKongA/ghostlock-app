package com.ghostlock.app.ui

import android.app.Application
import android.content.pm.ApplicationInfo

/**
 *  KernelSU 代码里的全局 Application 句柄（2026-10-18 恢复移植）。
 *
 *  上游由 KernelSUApplication 在 onCreate 里赋值；宿主已有 GhostLockApp，
 *  所以这里只保留一个可赋值的句柄，由宿主启动时写入。
 */
lateinit var ksuApp: Application

/**
 *  上游 KernelSUApplication 的静态工具替身。
 *
 *  只保留保留屏（配色页）需要的隐藏 API 反射入口，不再承载 Application 生命周期。
 */
object KernelSUApplication {

    /**
     * 打开/关闭系统的预测性返回回调（反射调隐藏 API）。
     *
     * @param appInfo 目标 ApplicationInfo。
     * @param enable true 打开，false 关闭。
     * @note 失败被静默吞掉：未接线构建不保证隐藏 API 可用。
     */
    fun setEnableOnBackInvokedCallback(appInfo: ApplicationInfo, enable: Boolean) {
        runCatching {
            val method = ApplicationInfo::class.java.getDeclaredMethod(
                "setEnableOnBackInvokedCallback",
                Boolean::class.javaPrimitiveType
            )
            method.isAccessible = true
            method.invoke(appInfo, enable)
        }
    }
}
