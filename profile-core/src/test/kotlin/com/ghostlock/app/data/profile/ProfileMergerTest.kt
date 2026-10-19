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
