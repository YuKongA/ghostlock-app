package com.ghostlock.app.ui.kit

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * 套件选择的三条规则：默认 Material、切一次换一个、选定的值立刻可读。
 *
 * 这些是纯 Kotlin 规则（存储是可替换的），所以它们在 JVM 单测里跑；SharedPreferences 那一层
 * 由真机/临时界面验证，不在这里假装验证。
 */
class UiKitControllerTest {
    @Test
    fun defaultIsMaterial() {
        assertEquals(UiKit.Material, UiKitController(InMemoryUiKitStore()).current)
    }

    @Test
    fun toggleGoesBackAndForth() {
        val controller = UiKitController(InMemoryUiKitStore())
        assertEquals(UiKit.Miuix, controller.toggle())
        assertEquals(UiKit.Miuix, controller.current)
        assertEquals(UiKit.Material, controller.toggle())
        assertEquals(UiKit.Material, controller.current)
    }

    @Test
    fun selectIsImmediate() {
        val store = InMemoryUiKitStore()
        val controller = UiKitController(store)
        controller.select(UiKit.Miuix)
        assertEquals(UiKit.Miuix, store.read())
        controller.select(UiKit.Material)
        assertEquals(UiKit.Material, store.read())
    }
}
