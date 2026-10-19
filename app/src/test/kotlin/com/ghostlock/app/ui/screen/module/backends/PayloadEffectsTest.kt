package com.ghostlock.app.ui.screen.module.backends

import org.junit.Assert.assertEquals
import org.junit.Test

/** 效果探针的三态与总判。 */
class PayloadEffectsTest {

    @Test
    fun ksuVerifiedWhenSuExists() {
        val reports = PayloadEffects.evaluate(
            FinishState(rootManagerEnabled = true, rootManager = "KernelSU"),
            exists = { it == "/system/bin/su" },
            modules = { "" },
        )
        assertEquals(EffectState.Verified, reports.single { it.item == "root" }.state)
    }

    @Test
    fun ksuUnknownWhenNothingObservable() {
        val reports = PayloadEffects.evaluate(
            FinishState(rootManagerEnabled = true, rootManager = "KernelSU"),
            exists = { false },
            modules = { "" },
        )
        assertEquals(EffectState.Unknown, reports.single { it.item == "root" }.state)
    }

    @Test
    fun lkmVerifiedFromProcModules() {
        val reports = PayloadEffects.evaluate(
            FinishState(lkmEnabled = true, lkmUri = "file:///data/x/custom.ko"),
            exists = { false },
            modules = { "custom 12345 0 - Live 0x0000000000000000\n" },
        )
        assertEquals(EffectState.Verified, reports.single { it.item == "lkm" }.state)
    }

    @Test
    fun appAndCommandStayUnknown() {
        val reports = PayloadEffects.evaluate(
            FinishState(runAppEnabled = true, appPackage = "com.example", scriptEnabled = true),
            exists = { false },
            modules = { "" },
        )
        assertEquals(EffectState.Unknown, reports.single { it.item == "app" }.state)
        assertEquals(EffectState.Unknown, reports.single { it.item == "cmd" }.state)
    }

    @Test
    fun blankAppPackageIsFailure() {
        val reports = PayloadEffects.evaluate(
            FinishState(runAppEnabled = true, appPackage = ""),
            exists = { false },
            modules = { "" },
        )
        assertEquals(EffectState.Failed, reports.single { it.item == "app" }.state)
    }

    @Test
    fun selinuxReadsEnforceFlag() {
        val on = PayloadEffects.evaluate(
            FinishState(restoreSelinux = true),
            exists = { true },
            modules = { "" },
            enforcing = { true },
        )
        assertEquals(EffectState.Verified, on.single { it.item == "selinux" }.state)
        val off = PayloadEffects.evaluate(FinishState(restoreSelinux = true), exists = { true }, modules = { "" }, enforcing = { false })
        assertEquals(EffectState.Failed, off.single { it.item == "selinux" }.state)
    }

    @Test
    fun overallRules() {
        assertEquals(
            EffectState.Failed,
            PayloadEffects.overall(listOf(EffectReport("a", EffectState.Verified, ""), EffectReport("b", EffectState.Failed, ""))),
        )
        assertEquals(
            EffectState.Unknown,
            PayloadEffects.overall(listOf(EffectReport("a", EffectState.Verified, ""), EffectReport("b", EffectState.Unknown, ""))),
        )
        assertEquals(
            EffectState.Verified,
            PayloadEffects.overall(listOf(EffectReport("a", EffectState.Verified, ""))),
        )
        assertEquals(EffectState.Unknown, PayloadEffects.overall(emptyList()))
    }
}
