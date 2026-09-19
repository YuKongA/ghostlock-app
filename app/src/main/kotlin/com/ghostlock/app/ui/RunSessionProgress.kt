package com.ghostlock.app.ui

import android.content.Context
import com.ghostlock.app.R
import java.util.concurrent.atomic.AtomicInteger

/** Maps exploit log milestones to a single step counter for notifications (not full logs). */
object RunSessionProgress {
    const val TOTAL_STEPS = 6

    private val step = AtomicInteger(0)

    fun reset() {
        step.set(0)
    }

    fun onLogLine(rawLine: String) {
        val line = rawLine.trim().lowercase()
        if (line.contains("boot attempt") || line.contains("==== start ====")) {
            reset()
        }
        val next = when {
            line.contains("exploit start") -> 1
            line.contains("write 1 complete") ||
                line.contains("selinux already permissive") ||
                line.contains("selinux permissive") -> 2
            line.contains("perf_find_task done") -> 3
            line.contains("child is root") -> 4
            line.contains("child seccomp fully bypassed") ||
                line.contains("no app seccomp filter") ||
                line.contains("child seccomp filter bypassed") -> 5
            line.contains("exploit complete") ||
                line.contains("kernelsu ready") -> 6
            else -> step.get()
        }
        step.updateAndGet { current -> maxOf(current, next) }
    }

    fun currentStep(): Int = step.get().coerceIn(1, TOTAL_STEPS)

    fun notificationBody(context: Context): String {
        val current = currentStep()
        return context.getString(R.string.session_step_progress, current, TOTAL_STEPS)
    }
}
