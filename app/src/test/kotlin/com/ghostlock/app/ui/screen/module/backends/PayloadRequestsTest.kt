package com.ghostlock.app.ui.screen.module.backends

import org.junit.Assert.assertEquals
import org.junit.Test

class PayloadRequestsTest {
    @Test
    fun followsTheConfiguredOrder() {
        val state = FinishState(
            rootManagerEnabled = true,
            runAppEnabled = true,
            appPackage = "com.example.app",
            scriptEnabled = true,
            command = "id",
            lkmEnabled = true,
            order = listOf("lkm", "root", "cmd", "app"),
        )
        val context = PayloadContext(state, null, null, "/tmp/custom.ko")
        assertEquals(listOf("lkm", "root", "cmd", "app"), state.payloadRequests(context).map { it.id })
    }

    @Test
    fun dropsDisabledPayloads() {
        val state = FinishState(
            rootManagerEnabled = false,
            runAppEnabled = false,
            scriptEnabled = false,
            lkmEnabled = false,
            order = listOf("root", "app", "cmd", "lkm"),
        )
        val context = PayloadContext(state, null, null, null)
        assertEquals(emptyList<String>(), state.payloadRequests(context).map { it.id })
    }
}
