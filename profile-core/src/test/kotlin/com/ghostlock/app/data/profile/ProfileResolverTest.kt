package com.ghostlock.app.data.profile

import com.ghostlock.app.data.valueMapOf
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ProfileResolverTest {
    @Test
    fun `nativeValue folds selected cpus into the recommended slots`() {
        val profile = valueMapOf(
            "execution" to valueMapOf("selected_cpus" to valueMapOf("main" to 3)),
        )
        assertEquals(
            3L,
            ProfileResolver.nativeValue(profile, "tcp_zerocopy", "none", "execution.recommended_cpus.main"),
        )
    }

    @Test
    fun `nativeValue maps the branch fields onto route then fallback`() {
        val profile = valueMapOf(
            "route" to valueMapOf("tcp_zerocopy" to valueMapOf("compact_waiter" to 1)),
        )
        assertEquals(1L, ProfileResolver.nativeValue(profile, "tcp_zerocopy", "none", "compact_waiter"))

        val fallbackOnly = valueMapOf(
            "fallback" to valueMapOf(
                "route" to valueMapOf("select_stack" to valueMapOf("waiter_shift" to -2)),
            ),
        )
        assertEquals(
            -2L,
            ProfileResolver.nativeValue(fallbackOnly, "tcp_zerocopy", "select_stack", "pselect_waiter_shift"),
        )
    }

    @Test
    fun `nativeValue maps mcast fields onto the route branch`() {
        val profile = valueMapOf(
            "route" to valueMapOf("multicast_waiter" to valueMapOf("waiter_off" to 96)),
        )
        assertEquals(96L, ProfileResolver.nativeValue(profile, "multicast_waiter", "none", "mcast.waiter_off"))
    }

    @Test
    fun `validateMerged rejects an unknown top level key`() {
        val errors = ProfileResolver.validateMerged(validProfile() + ("bogus" to 1L), "select_stack", "none")
        assertTrue(errors.any { it.fieldPath == "bogus" })
    }

    @Test
    fun `validateMerged rejects a missing required task field`() {
        val profile = validProfile()
        (profile["task_struct"] as MutableMap<String, Any?>).remove("prio")
        val errors = ProfileResolver.validateMerged(profile, "select_stack", "none")
        assertTrue(errors.any { it.fieldPath == "task_struct.prio" })
    }

    @Test
    fun `validateMerged accepts a minimal valid profile`() {
        assertEquals(emptyList<ConfigError>(), ProfileResolver.validateMerged(validProfile(), "select_stack", "none"))
    }

    @Test
    fun `executionFromMerged flattens nested values`() {
        val profile = valueMapOf(
            "execution" to valueMapOf(
                "stages" to valueMapOf("w1_attempts" to 15),
                "recommended_cpus" to valueMapOf("main" to 0),
            ),
        )
        val flat = ProfileResolver.executionFromMerged(profile)
        assertEquals(15uL, flat["stages.w1_attempts"])
        assertEquals(0uL, flat["recommended_cpus.main"])
    }

    private fun validProfile(): MutableMap<String, Any?> = valueMapOf(
        "release" to "test",
        "schema_version" to 1,
        "kernel_major" to 6,
        "route" to valueMapOf("select_stack" to valueMapOf("waiter_shift" to 0)),
        "task_struct" to valueMapOf(
            "prio" to 1, "normal_prio" to 1, "sched_task_group" to 1, "pi_lock" to 1,
            "pi_waiters" to 1, "pi_top_task" to 1, "pi_blocked_on" to 1, "pid" to 1,
            "tgid" to 1, "atomic_flags" to 1, "real_cred" to 1, "cred" to 1,
            "comm" to 1, "tasks" to 1, "seccomp" to 1,
        ),
        "cred" to valueMapOf("copy_size" to 136, "caps_count" to 5),
        "offset" to valueMapOf(
            "init_task" to 1, "init_cred" to 1, "root_task_group" to 1, "selinux_enforcing" to 1,
        ),
    )
}
