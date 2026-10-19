package com.ghostlock.app.ui.kit

/**
 * 界面套件：同一套界面可以按 Material 或 Miuix 渲染（用户裁定 2026-10-14：新 UI 用 Material，
 * 并像 KernelSU 那样可以灵活切到 Miuix）。
 *
 * 这里只有"选择"这件事：读哪个套件、写到哪、怎么切。渲染在 [GhostlockKit] 的两个实现里，
 * 主题在 com.ghostlock.app.ui.theme.GhostlockTheme 里，三者各自只管一件。
 */
/** 当前套件（语义组件面）：页面只读它，不 import 具体控件。 */
val LocalUiKit = androidx.compose.runtime.staticCompositionLocalOf<GhostlockKit> {
    error("UiKit not provided")
}

enum class UiKit {
    Material,
    Miuix,
}

/** 选择结果的存放处：真实实现是 SharedPreferences（见 AndroidUiKitStore），测试用内存实现。 */
interface UiKitStore {
    fun read(): UiKit
    fun write(kit: UiKit)
}

/** 一个不会碰磁盘的实现：单测与临时界面预览用。 */
class InMemoryUiKitStore(private var kit: UiKit = UiKit.Material) : UiKitStore {
    override fun read(): UiKit = kit

    override fun write(kit: UiKit) {
        this.kit = kit
    }
}

/**
 * 套件选择的唯一入口。
 *
 * 读永远是"现在盘上是什么"，写立刻落盘 —— 没有内存副本，所以两个界面（设置页与主页）看到
 * 的永远是同一个值，切换不需要重启进程。
 */
class UiKitController(private val store: UiKitStore) {
    /** 当前套件。 */
    val current: UiKit
        get() = store.read()

    /** 换成另一个套件，并回答换成了哪个。 */
    fun toggle(): UiKit {
        val next = if (current == UiKit.Material) UiKit.Miuix else UiKit.Material
        store.write(next)
        return next
    }

    /** 直接选定（设置页用）。 */
    fun select(kit: UiKit) {
        store.write(kit)
    }
}
