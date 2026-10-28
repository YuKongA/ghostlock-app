package com.ghostlock.app.ui.backend

import android.os.Process
import rikka.shizuku.Shizuku
import com.ghostlock.app.ui.screen.home.ExecutorMode
import com.ghostlock.app.ui.screen.home.ExecutorPreference

/**
 * backend 声明的**执行体 uid 需求**（只声明；检查/拒绝由 UI 负责，backend 不自检）。
 *
 * @note UI 不解析 Shizuku 的实际 uid：执行体只有两种，授权状态是二值——
 *   App 攻击线程（app uid）或 Shizuku 攻击线程（shell uid 2000，仅已授权时可用）。
 */
enum class BackendUidRequirement {
    /** 任意执行体（App/Shizuku 均可；lua/43499/43284）。 */
    ANY_UID,

    /** app 攻击线程（app 自身 uid）。 */
    APP_UID,

    /** Shizuku 攻击线程（shell uid 2000）。 */
    SHELL_UID_2000,
    ;

    /**
     * @param kind 攻击线程执行体种类（不由 backend 探测）。
     * @return true=该执行体满足声明（授权状态由 UI 单独判定）。
     */
    fun satisfiedBy(kind: AttackExecutorKind): Boolean = when (this) {
        ANY_UID -> true
        APP_UID -> kind == AttackExecutorKind.APP
        SHELL_UID_2000 -> kind == AttackExecutorKind.SHIZUKU
    }
}

/** 攻击线程执行体种类（由 UI 的选择决定）。 */
enum class AttackExecutorKind {
    /** app 进程 JVM 攻击线程。 */
    APP,

    /** Shizuku UserService JVM 攻击线程（shell 域）。 */
    SHIZUKU,
}

/**
 * UI 侧执行体状态：种类 + **授权二值**（不读取/解析 uid）。
 *
 * @property kind 选中的攻击线程种类。
 * @property authorized 该执行体是否可用（App 恒 true；Shizuku=已启动且已授权）。
 */
data class AttackExecutorState(
    val kind: AttackExecutorKind,
    val authorized: Boolean,
)

/** @return Shizuku 是否可用且已授权（二值，不取 uid）。 */
private fun shizukuAuthorized(): Boolean = runCatching {
    Shizuku.pingBinder() &&
        Shizuku.checkSelfPermission() == android.content.pm.PackageManager.PERMISSION_GRANTED
}.getOrDefault(false)

/**
 * @return UI 当前选中的攻击线程状态（只按执行体选择 + Shizuku 授权二值）。
 */
internal fun currentAttackExecutorState(): AttackExecutorState = when (ExecutorPreference.mode) {
    ExecutorMode.App -> AttackExecutorState(AttackExecutorKind.APP, authorized = true)
    ExecutorMode.Shizuku -> AttackExecutorState(AttackExecutorKind.SHIZUKU, shizukuAuthorized())
}
