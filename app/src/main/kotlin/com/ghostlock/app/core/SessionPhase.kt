package com.ghostlock.app.core

/** Native 会话的生命周期；DIRTY 永不回到 READY。 */
enum class SessionPhase {
    INITIALIZING,
    READY,
    CLOSING,
    CLOSED,
    FAILED,
    DIRTY,
}
