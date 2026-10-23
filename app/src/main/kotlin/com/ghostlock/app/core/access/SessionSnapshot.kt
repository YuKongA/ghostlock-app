package com.ghostlock.app.core.access

import com.ghostlock.app.core.SessionPhase

/** 会话快照；只描述可观察状态，不暴露 native 句柄。 */
data class SessionSnapshot(
    val generation: Long,
    val phase: SessionPhase,
)
