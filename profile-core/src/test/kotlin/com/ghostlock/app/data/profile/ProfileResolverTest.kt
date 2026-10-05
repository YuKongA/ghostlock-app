package com.ghostlock.app.data.profile

import com.ghostlock.app.data.HoconSupport
import com.ghostlock.app.data.NativeProfileDocument
import com.ghostlock.app.data.component.CombinationCatalog
import com.ghostlock.app.data.asValueMap
import com.ghostlock.app.data.valueMapOf
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
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
            ProfileResolver.nativeValue(profile, "tcp_zerocopy", "execution.recommended_cpus.main"),
        )
    }

    @Test
    fun `nativeValue maps the branch fields onto the route branch`() {
        val profile = valueMapOf(
            "route" to valueMapOf("tcp_zerocopy" to valueMapOf("compact_waiter" to 1)),
        )
        assertEquals(1L, ProfileResolver.nativeValue(profile, "tcp_zerocopy", "compact_waiter"))

        /* The shipped profiles spell compact_waiter as a HOCON boolean; the
         * route branch lookup folds true/false onto the wire's 1/0. */
        val booleanBranch = valueMapOf(
            "route" to valueMapOf("tcp_zerocopy" to valueMapOf("compact_waiter" to true)),
        )
        assertEquals(1L, ProfileResolver.nativeValue(booleanBranch, "tcp_zerocopy", "compact_waiter"))
        val offBranch = valueMapOf(
            "route" to valueMapOf("tcp_zerocopy" to valueMapOf("compact_waiter" to false)),
        )
        assertEquals(0L, ProfileResolver.nativeValue(offBranch, "tcp_zerocopy", "compact_waiter"))
    }

    @Test
    fun `nativeValue maps mcast fields onto the route branch`() {
        val profile = valueMapOf(
            "route" to valueMapOf("multicast_waiter" to valueMapOf("waiter_off" to 96)),
        )
        assertEquals(96L, ProfileResolver.nativeValue(profile, "multicast_waiter", "mcast.waiter_off"))
    }

    @Test
    fun `validateMerged rejects an unknown top level key`() {
        val errors = ProfileResolver.validateMerged(validProfile() + ("bogus" to 1L), "select_stack")
        assertTrue(errors.any { it.fieldPath == "bogus" })
    }

    @Test
    fun `validateMerged rejects a missing required task field`() {
        val profile = validProfile()
        profile["task_struct"].asValueMap()!!.remove("prio")
        val errors = ProfileResolver.validateMerged(profile, "select_stack")
        assertTrue(errors.any { it.fieldPath == "task_struct.prio" })
    }

    @Test
    fun `validateMerged reports a missing required cred field`() {
        val profile = validProfile()
        profile["cred"].asValueMap()!!.remove("copy_size")
        val errors = ProfileResolver.validateMerged(profile, "select_stack")
        assertTrue(errors.any { it.fieldPath == "cred.copy_size" && it.message == "missing" })
    }

    @Test
    fun `validateMerged accepts a minimal valid profile`() {
        assertEquals(emptyList<ConfigError>(), ProfileResolver.validateMerged(validProfile(), "select_stack"))
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

    @Test
    fun `validateMerged rejects tuning values outside the native widths`() {
        fun multicast(attempts: Long, armSequence: Long, armHold: Long) = validProfile() + mapOf(
            "route" to valueMapOf(
                "multicast_waiter" to valueMapOf(
                    "attempts" to attempts, "arm_sequence" to armSequence, "arm_hold" to armHold,
                ),
            ),
        )
        val tooWide = ProfileResolver.validateMerged(
            multicast(attempts = 256, armSequence = 16, armHold = 20000), "multicast_waiter",
        )
        assertTrue(tooWide.any { it.fieldPath == "route.multicast_waiter.attempts" })
        val holdWide = ProfileResolver.validateMerged(
            multicast(attempts = 128, armSequence = 16, armHold = 65536), "multicast_waiter",
        )
        assertTrue(holdWide.any { it.fieldPath == "route.multicast_waiter.arm_hold" })
        assertEquals(
            emptyList<ConfigError>(),
            ProfileResolver.validateMerged(
                multicast(attempts = 128, armSequence = 16, armHold = 20000), "multicast_waiter",
            ),
        )
    }

    @Test
    fun `validateMerged rejects a vr guard layout the transport cannot carry`() {
        val profile = validProfile() + ("vr_guard" to valueMapOf("tracepoint_funcs" to 0x140L))
        val errors = ProfileResolver.validateMerged(profile, "select_stack")
        assertTrue(errors.any { it.fieldPath == "vr_guard.tracepoint_funcs" })
    }

    @Test
    fun `nativeText resolves a dotted string path`() {
        val profile = valueMapOf("backend" to valueMapOf("steps" to "w1_w3"))
        assertEquals("w1_w3", ProfileResolver.nativeText(profile, "backend.steps"))
        assertNull(ProfileResolver.nativeText(profile, "backend.missing"))
        assertNull(ProfileResolver.nativeText(profile, "backend"))
    }

    @Test
    fun `nativeBool accepts booleans and HOCON string spellings`() {
        val profile = valueMapOf(
            "flag_bool" to true,
            "flag_true" to " TRUE ",
            "flag_one" to "1",
            "flag_yes" to "yes",
            "flag_on" to "On",
            "flag_false" to "false",
            "flag_zero" to "0",
            "flag_no" to "no",
            "flag_off" to "off",
            "flag_bogus" to "maybe",
            "flag_number" to 1,
        )
        assertEquals(true, ProfileResolver.nativeBool(profile, "flag_bool"))
        assertEquals(true, ProfileResolver.nativeBool(profile, "flag_true"))
        assertEquals(true, ProfileResolver.nativeBool(profile, "flag_one"))
        assertEquals(true, ProfileResolver.nativeBool(profile, "flag_yes"))
        assertEquals(true, ProfileResolver.nativeBool(profile, "flag_on"))
        assertEquals(false, ProfileResolver.nativeBool(profile, "flag_false"))
        assertEquals(false, ProfileResolver.nativeBool(profile, "flag_zero"))
        assertEquals(false, ProfileResolver.nativeBool(profile, "flag_no"))
        assertEquals(false, ProfileResolver.nativeBool(profile, "flag_off"))
        assertNull(ProfileResolver.nativeBool(profile, "flag_bogus"))
        assertNull(ProfileResolver.nativeBool(profile, "flag_number"))
        assertNull(ProfileResolver.nativeBool(profile, "flag_missing"))
    }

    @Test
    fun `HOCON backend steps migrates the legacy step id to a combination token`() {
        val merged = requireNotNull(HoconSupport.parseValue("{ backend { steps = \"w1_w3\" } }").asValueMap())
        val document = NativeProfileDocument.from(
            release = "test",
            route = "select_stack",
            value = { path -> ProfileResolver.nativeValue(merged, "select_stack", path) },
            text = { path -> ProfileResolver.nativeText(merged, path) },
        )
        assertEquals(requireNotNull(CombinationCatalog.resolve("pselect_rootchild")) { "pselect_rootchild" }, document.combination)
        assertEquals("pselect_rootchild", document.combination?.token)
    }

    @Test
    fun `native document rejects an unknown combination token with its text`() {
        val merged = requireNotNull(
            HoconSupport.parseValue("{ backend { steps = \"bogus_token\" } }").asValueMap(),
        )
        val error = org.junit.Assert.assertThrows(IllegalArgumentException::class.java) {
            NativeProfileDocument.from(
                release = "test",
                route = "select_stack",
                value = { path -> ProfileResolver.nativeValue(merged, "select_stack", path) },
                text = { path -> ProfileResolver.nativeText(merged, path) },
            )
        }
        org.junit.Assert.assertTrue(error.message!!.contains("bogus_token"))
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
