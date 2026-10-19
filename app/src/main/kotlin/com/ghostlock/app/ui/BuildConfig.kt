package com.ghostlock.app.ui

/**
 *  KernelSU UI 的构建常量替身（2026-10-18 恢复移植）。
 *
 *  上游这些常量由 app 模块的 BuildConfig 生成；这里 UI 代码已并入宿主 app，
 *  但仍按上游写法引用 com.ghostlock.app.ui.BuildConfig，故用固定样例补上。
 */
object BuildConfig {
    /** 管理器版本名。 */
    const val VERSION_NAME = "1.3"

    /** 管理器版本号。 */
    const val VERSION_CODE = 1328

    /** 是否为 PR 构建（未接线构建恒为 false）。 */
    const val IS_PR_BUILD = false

    /** 宿主 applicationId。 */
    const val APPLICATION_ID = "com.ghostlock.app"
}
