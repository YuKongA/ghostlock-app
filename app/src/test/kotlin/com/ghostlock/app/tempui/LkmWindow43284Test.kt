package com.ghostlock.app.tempui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * LKM 驻留窗口的幂等与失败语义（照 ghostlock-app-ref@5f184760:steps/chain.cpp:362-380）。
 */
class LkmWindow43284Test {
    @Test
    fun openingTwiceOpensOnce() {
        var opens = 0
        val window = LkmWindow43284(openFn = { opens += 1; 1 }, closeFn = { true })
        assertTrue(window.open())
        assertTrue(window.open())
        assertEquals(1, opens)
    }

    @Test
    fun aFailedOpenLeavesTheWindowClosedAndDoesNotThrow() {
        val window = LkmWindow43284(openFn = { -1 }, closeFn = { true })
        assertFalse(window.open())
        assertFalse(window.isOpen())
    }

    @Test
    fun closingWithoutOpeningIsSuccessful() {
        val window = LkmWindow43284(openFn = { 1 }, closeFn = { true })
        assertTrue(window.close())
    }

    @Test
    fun openCloseOpenIsAllowed() {
        val window = LkmWindow43284(openFn = { 1 }, closeFn = { true })
        assertTrue(window.open())
        assertTrue(window.close())
        assertTrue(window.open())
        assertTrue(window.isOpen())
    }

    @Test
    fun aFailedCloseKeepsTheWindowOpen() {
        val window = LkmWindow43284(openFn = { 1 }, closeFn = { false })
        assertTrue(window.open())
        assertFalse(window.close())
        assertTrue(window.isOpen())
    }
}
