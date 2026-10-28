package com.ghostlock.app.backend.cve43499.owned

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * task-96 NO_PIN 哨兵单测（先写不运行）。
 *
 * G08 debug：owner_cpu=-1/waiter_cpu=-1 = NO_PIN（Native 0xFFFFFFFF 不 pin），consumer_cpu=2；
 * 本地 precheck 必须放行 owner/waiter=-1、仍拒绝 consumer<0 与其它负值。
 */
class ActorCpuPinningTest {

    @Test
    fun `no pin sentinel for owner and waiter is accepted`() {
        assertNull(precheckActorCpuPinning(ownerCpu = -1, waiterCpu = -1, consumerCpu = 2))
        assertNull(precheckActorCpuPinning(ownerCpu = 0, waiterCpu = 1, consumerCpu = 2))
    }

    @Test
    fun `consumer must be real cpu and other negatives are rejected`() {
        assertEquals(
            "actor-cpu:consumer",
            precheckActorCpuPinning(ownerCpu = -1, waiterCpu = -1, consumerCpu = -1),
        )
        assertEquals(
            "actor-cpu:owner",
            precheckActorCpuPinning(ownerCpu = -2, waiterCpu = 0, consumerCpu = 2),
        )
        assertEquals(
            "actor-cpu:waiter",
            precheckActorCpuPinning(ownerCpu = 0, waiterCpu = -2, consumerCpu = 2),
        )
    }
}
