package com.ghostlock.app.data.profile

import com.ghostlock.app.data.asValueMap
import com.ghostlock.app.data.valueMapOf
import org.junit.Assert.assertEquals
import org.junit.Test

class ProfileMergerTest {
    @Test
    fun `sparse route group is filled field-wise from the preset`() {
        val profile = valueMapOf(
            "execution" to valueMapOf(
                "routes" to valueMapOf(
                    "select_stack" to valueMapOf("enter_delay_us" to 1L),
                ),
            ),
        )
        val preset = valueMapOf("enter_delay_us" to 50000L, "timeout_us" to 200000L)
        ProfileMerger.fillRouteExecutionDefaults(profile, mapOf("select_stack" to preset))

        val group = profile["execution"].asValueMap()!!["routes"].asValueMap()!!
            .get("select_stack").asValueMap()!!
        assertEquals(1L, group["enter_delay_us"])   // explicit value preserved
        assertEquals(200000L, group["timeout_us"])  // missing field filled from preset
    }

    @Test
    fun `a builtin execution block does not mutate the shared tuning map`() {
        val tuning = valueMapOf(
            "recommended_cpus" to valueMapOf("main" to 0L, "consumer" to 1L),
            "stages" to valueMapOf("w1_attempts" to 15L),
        )
        val first = valueMapOf(
            "release" to "a",
            "kernel_major" to 6L,
            "execution" to valueMapOf(
                "recommended_cpus" to valueMapOf("main" to 4L, "consumer" to 5L),
                "stages" to valueMapOf("w1_attempts" to 25L),
            ),
        )
        val second = valueMapOf("release" to "b", "kernel_major" to 6L)

        ProfileMerger.resolveMerged("a", first, null, null, tuning, CpuPairView(0, 1), emptyMap())
        val resolved = ProfileMerger.resolveMerged(
            "b", second, null, null, tuning, CpuPairView(0, 1), emptyMap(),
        )

        val execution = resolved["execution"].asValueMap()!!
        assertEquals(15L, execution["stages"].asValueMap()!!["w1_attempts"])
        val cpus = execution["recommended_cpus"].asValueMap()!!
        assertEquals(0L, cpus["main"])
        assertEquals(1L, cpus["consumer"])
    }

    @Test
    fun `cpu pair outranks an imported selected_cpus`() {
        val imported = valueMapOf(
            "execution" to valueMapOf(
                "selected_cpus" to valueMapOf("main" to 7L, "consumer" to 8L),
            ),
        )
        val builtin = valueMapOf(
            "release" to "r",
            "kernel_major" to 6L,
            "execution" to valueMapOf(),
        )
        val resolved = ProfileMerger.resolveMerged(
            deviceRelease = "r",
            builtin = builtin,
            imported = imported,
            overrides = null,
            tuningExecution = null,
            pair = CpuPairView(3, 4),
            routePresets = emptyMap(),
        )
        val selected = resolved["execution"].asValueMap()!!["selected_cpus"].asValueMap()!!
        assertEquals(3L, selected["main"])
        assertEquals(4L, selected["consumer"])
    }
}
