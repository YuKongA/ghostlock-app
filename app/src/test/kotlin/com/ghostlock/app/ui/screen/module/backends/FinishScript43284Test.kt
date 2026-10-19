package com.ghostlock.app.ui.screen.module.backends

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class FinishScript43284Test {

    @Test
    fun relayAccessHandshakeIsEmittedWhenEnabled() {
        val state = FinishState(lkmEnabled = true, lkmUri = "content://relay", kernelAccessAfterRelay = true)
        val script = state.toRootScript(
            managerBinary = null,
            stagedScript = null,
            stagedLkm = "/data/user/0/com.ghostlock.app/files/finish/custom.ko",
        )
        assertTrue(script.contains("ACCESS_READY=/data/local/tmp/ghostlock-lkm-ready"))
        assertTrue(script.contains("ACCESS_CONNECTED=/data/local/tmp/ghostlock-lkm-connected"))
        assertTrue(script.contains("sleep 0.1"))
        assertTrue(script.contains("kernel access timeout"))
        assertTrue(script.contains("wait for close"))
    }

    @Test
    fun relayAccessHandshakeIsAbsentWhenDisabled() {
        val state = FinishState(lkmEnabled = true, lkmUri = "content://relay", kernelAccessAfterRelay = false)
        val script = state.toRootScript(
            managerBinary = null,
            stagedScript = null,
            stagedLkm = "/data/user/0/com.ghostlock.app/files/finish/custom.ko",
        )
        assertFalse(script.contains("ghostlock-lkm-ready"))
        assertTrue(script.contains("insmod"))
    }


    @Test
    fun folkPatchUsesLaunchOnlyPath() {
        val state = FinishState(rootManagerEnabled = true, rootManager = "FolkPatch")
        val script = state.toRootScript(
            managerBinary = null,
            stagedScript = null,
            stagedLkm = null,
        )
        assertTrue(script.contains("apd present"))
        assertTrue(script.contains("monkey -p me.yuki.folk"))
        assertFalse(script.contains("libksud.so"))
    }


    @Test
    fun folkPatchJailbreakLoadsKoAndWritesMarker() {
        val state = FinishState(
            rootManagerEnabled = true,
            rootManager = "FolkPatch",
            lkmEnabled = true,
            lkmUri = "content://kernelpatch.ko",
        )
        val script = state.toRootScript(
            managerBinary = null,
            stagedScript = null,
            stagedLkm = "/data/user/0/com.ghostlock.app/files/finish/custom.ko",
        )
        assertTrue(script.contains("APD=/data/adb/apd"))
        assertTrue(script.contains("libapd.so"))
        assertTrue(script.contains("late-load --module"))
        assertTrue(script.contains("> /data/adb/ap/jailbreak"))
        assertTrue(script.contains("setenforce 0"))
    }
}
