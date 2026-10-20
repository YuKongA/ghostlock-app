package com.ghostlock.app.autostart

import android.app.Application
import android.content.Context
import androidx.core.content.edit
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class BootGuardTest {
    private val context: Application = RuntimeEnvironment.getApplication()

    private fun prefs(name: String) =
        context.getSharedPreferences(name, Context.MODE_PRIVATE)

    /** Guard over a mutable fake boot id so each test drives the boot line. */
    private class Fixture(prefsName: String = "boot_guard_test") {
        var bootId = "B1"
        val guard = BootGuard(
            RuntimeEnvironment.getApplication()
                .getSharedPreferences(prefsName, Context.MODE_PRIVATE),
        ) { bootId }
    }

    private val enabled = BootGuardState(
        enabled = true,
        attemptCount = 0,
        attemptBootId = "B1",
        goodBootId = "B1",
    )

    @Test
    fun `three unconfirmed reboots run then the next boot trips the guard`() {
        var state = enabled
        for (boot in listOf("B2", "B3", "B4")) {
            val (next, decision) = BootGuardTransitions.nextOnBoot(state, boot)
            state = next
            assertEquals(BootDecision.RUN, decision)
        }
        assertEquals(3, state.attemptCount)

        val (tripped, decision) = BootGuardTransitions.nextOnBoot(state, "B5")
        assertEquals(BootDecision.GUARD_TRIPPED, decision)
        assertFalse(tripped.enabled)
        assertTrue("the UI must warn after a trip", tripped.noticePending)
        assertEquals(0, tripped.attemptCount)
    }

    @Test
    fun `a confirmation before the next reboot restarts the count`() {
        val (afterBoot, decision) = BootGuardTransitions.nextOnBoot(enabled, "B2")
        assertEquals(BootDecision.RUN, decision)
        val confirmed = BootGuardTransitions.nextOnConfirm(afterBoot, "B2")
        checkNotNull(confirmed)
        assertEquals(0, confirmed.attemptCount)
        assertEquals("B2", confirmed.goodBootId)

        val (next, nextDecision) = BootGuardTransitions.nextOnBoot(confirmed, "B3")
        assertEquals(BootDecision.RUN, nextDecision)
        assertEquals(1, next.attemptCount)
    }

    @Test
    fun `repeat boot broadcasts within one boot are deduped`() {
        val (afterBoot, first) = BootGuardTransitions.nextOnBoot(enabled, "B2")
        assertEquals(BootDecision.RUN, first)
        val (same, second) = BootGuardTransitions.nextOnBoot(afterBoot, "B2")
        assertEquals(BootDecision.ALREADY_HANDLED, second)
        assertEquals(afterBoot, same)
    }

    @Test
    fun `confirming an uncounted or already good boot changes nothing`() {
        assertNull(BootGuardTransitions.nextOnConfirm(enabled, "B7"))
        assertNull(BootGuardTransitions.nextOnConfirm(enabled, ""))
        val confirmed = BootGuardTransitions.nextOnConfirm(enabled.copy(attemptBootId = "B1"), "B1")
        assertNull("boot already marked good must not rewrite state", confirmed)
    }

    @Test
    fun `disabled auto-start runs nothing`() {
        val (state, decision) = BootGuardTransitions.nextOnBoot(BootGuardState(), "B2")
        assertEquals(BootDecision.DISABLED, decision)
        assertEquals(state, BootGuardState())
    }

    @Test
    fun `enabling adopts the current boot and clears a pending notice`() {
        val tripped = BootGuardState(
            enabled = false,
            attemptCount = 0,
            attemptBootId = "B4",
            goodBootId = "B1",
            noticePending = true,
        )
        val state = BootGuardTransitions.nextOnEnable(tripped, "B5")
        assertTrue(state.enabled)
        assertEquals("B5", state.attemptBootId)
        assertEquals("B5", state.goodBootId)
        assertFalse(state.noticePending)
        val (next, decision) = BootGuardTransitions.nextOnBoot(state, "B5")
        assertEquals(BootDecision.ALREADY_HANDLED, decision)
        assertEquals(state, next)
    }

    @Test
    fun `the store survives the full boot sequence through preferences`() {
        val fixture = Fixture("boot_guard_flow")
        fixture.guard.enable()
        assertEquals(BootDecision.ALREADY_HANDLED, fixture.guard.onBoot())

        for ((index, boot) in listOf("B2", "B3", "B4").withIndex()) {
            fixture.bootId = boot
            assertEquals(BootDecision.RUN, fixture.guard.onBoot())
            assertTrue("attempt $index needs confirmation", fixture.guard.needsConfirmation())
            // Reboot without the 60 s mark surviving: nothing confirms this boot.
        }
        fixture.bootId = "B5"
        assertEquals(BootDecision.GUARD_TRIPPED, fixture.guard.onBoot())
        assertFalse(fixture.guard.read().enabled)
        assertTrue(fixture.guard.consumeNotice())
        assertFalse("the notice is one-shot", fixture.guard.consumeNotice())
    }

    @Test
    fun `confirmation writes only the current counted boot`() {
        val fixture = Fixture("boot_guard_confirm")
        fixture.guard.enable()
        fixture.bootId = "B2"
        assertEquals(BootDecision.RUN, fixture.guard.onBoot())

        fixture.bootId = "B3"
        fixture.guard.confirmCurrentBoot()
        assertEquals("stale confirmation must not mark a different boot", "B1", fixture.guard.read().goodBootId)

        fixture.bootId = "B2"
        assertTrue(fixture.guard.needsConfirmation())
        fixture.guard.confirmCurrentBoot()
        val state = fixture.guard.read()
        assertEquals("B2", state.goodBootId)
        assertEquals(0, state.attemptCount)
        assertFalse(fixture.guard.needsConfirmation())
    }

    @Test
    fun `unrelated preference keys are untouched`() {
        val name = "boot_guard_coexist"
        prefs(name).edit { putString("cpu_pair", "4,7") }
        val fixture = Fixture(name)
        fixture.guard.enable()
        fixture.bootId = "B2"
        fixture.guard.onBoot()
        assertEquals("4,7", prefs(name).getString("cpu_pair", null))
        assertFalse(prefs(name).contains("boot_guard_written_by_accident"))
    }
}
