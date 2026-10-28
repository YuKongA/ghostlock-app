package com.ghostlock.app.ui.backend

import com.ghostlock.app.ui.screen.module.backends.BackendCustom
import com.ghostlock.app.ui.screen.module.backends.BackendCve202643284
import com.ghostlock.app.ui.screen.module.backends.BackendCve202643499
import com.ghostlock.app.ui.screen.module.backends.BackendCve202698260
import com.ghostlock.app.ui.screen.module.backends.Backends
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * R1a 结构守卫：runner 是唯一可运行权威（spec 无默认 runner + capability 派生）。
 *
 * @note 纯 JVM；不触 Android/Flow 执行。
 */
class BackendFlowRunnerTest {
    @Test
    fun everySpecDeclaresRunnerAndCapabilityDerivesFromIt() {
        for (spec in Backends.all) {
            if (spec.runner == null) {
                assertEquals("no runner => NO_FLOW_RUN (" + spec.cve + ")", BackendRunCapability.NO_FLOW_RUN, spec.runCapability)
            } else {
                assertEquals("runner => FLOW_RUN (" + spec.cve + ")", BackendRunCapability.FLOW_RUN, spec.runCapability)
            }
        }
    }

    @Test
    fun runnerIdentityMatchesSpecKey() {
        for (spec in Backends.all) {
            val runner = spec.runner ?: continue
            assertNotNull("runner must declare key (" + spec.cve + ")", runner.key)
            assertEquals(spec.key, runner.key)
        }
    }

    @Test
    fun noFlowBackendsHaveNoRunner() {
        assertNull(BackendCve202643284.runner)
        assertNull(BackendCustom.runner)
        assertEquals(BackendRunCapability.NO_FLOW_RUN, BackendCve202643284.runCapability)
    }

    @Test
    fun flowBackendsPointAtTheirOwnRunner() {
        assertEquals(BackendCve202643499.key, BackendCve202643499.runner?.key)
        assertEquals(BackendCve202698260.key, BackendCve202698260.runner?.key)
        assertEquals(2, Backends.all.count { it.runner != null })
    }
}
