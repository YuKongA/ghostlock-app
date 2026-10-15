package com.ghostlock.app.tempui

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.ghostlock.app.core.AccessDomain
import com.ghostlock.app.core.AccessFailure
import com.ghostlock.app.core.SessionPhase
import com.ghostlock.app.jni.Native
import com.ghostlock.app.log.GhostLog
import com.ghostlock.app.log.LogLevel
import com.ghostlock.app.data.profile.AndroidAssetSource
import com.ghostlock.app.ui.kit.AndroidUiKitStore
import com.ghostlock.app.ui.kit.UiKit
import com.ghostlock.app.ui.kit.UiKitController
import com.ghostlock.app.ui.kit.kitOf
import com.ghostlock.app.ui.theme.GhostlockTheme

/** 临时界面的 logcat 标签（类与顶层 composable 共用一份）。 */
private const val TAG_UI = "GhostlockUi"

/**
 * 最小临时 UI：只做诊断，不做业务。
 *
 * 用户裁定（2026-10-14）：旧 Kotlin UI 全面废弃，测试期间只保留一个临时界面；新 UI 用 Material，
 * 并像 KernelSU 那样可以灵活切到 Miuix。所以这个界面本身就是那套切换的第一个用户：顶部一个按钮
 * 在两个套件之间切换（选择落 SharedPreferences，切完立刻重绘），下面的按钮在所有套件下都能用。
 *
 * 「设备门禁」按钮把 JNI 边界在本机走一遍，并把每一步写进 logcat（TAG=GhostlockGate），供 adb 抓取；
 * 界面启动时自动跑一次，所以一次 am start 就能拿到完整证据。
 *
 * 它不宣称任何攻击能力、不发起任何攻击步骤：那些要等设备 seam（J3b/J4b）与 Kotlin 步骤引擎（J6）。
 */
class TempUiActivity : ComponentActivity() {

    private val controller: UiKitController by lazy { UiKitController(AndroidUiKitStore(this)) }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            var kit by remember { mutableStateOf(controller.current) }
            GhostlockTheme(kit) {
                TempUiScreen(
                    kit = kit,
                    onToggleKit = { kit = controller.toggle() },
                    onProbe = ::probeNative,
                    onContracts = ::reportContracts,
                    onGate = ::runDeviceGate,
                    onAttack = ::runAttack,
                    onPlan = ::runPlans,
                )
            }
        }
    }

    private fun probeNative(): List<String> {
        val loaded = Native.ensureLoaded()
        return listOf(
            "jni: 库名=" + Native.LIBRARY + " 可加载=" + loaded,
            "jni: 入口=create/invoke/shutdown/destroy（四入口；GLKJ 帧 + 会话状态机已落地）",
        )
    }

    private fun reportContracts(): List<String> = listOf(
        "core: AccessDomain=" + AccessDomain.entries.joinToString(),
        "core: AccessFailure=" + AccessFailure.entries.joinToString(),
        "core: SessionPhase=" + SessionPhase.entries.joinToString(),
        "core: 能力接口=KernelMemoryAccess / PageCacheAccess（描述符由 native 发布）",
    )

    /** 跑两个 backend 的声明计划；每一步的结局见 logcat 的 GhostlockPlan。 */
    private fun runPlans(): List<String> {
        GhostLog.i(TAG_UI, "tempui: run the declared plans")
        val source = AndroidAssetSource(assets)
        val release = System.getProperty("os.version") ?: "unknown"
        val outcomes = PlanRunner.run(release, source, source)
        return outcomes.map { outcome -> outcome.summary() }
    }

    /**
     *  完整攻击（43284）：patch#1 → 载体 → hook → trigger → wait，一次点完。
     *
     *  @note 链子本身在**工作线程**里跑（Android 禁止主线程做网络/重 IO），但这里 **join 回来**，
     *        所以对调用者仍是同步语义 —— 房间里没有留下没被等的异步。
     *  @return 每一步的结局（与 logcat 的 GhostlockTyped 相同）。
     */
    private fun runAttack(): List<String> {
        GhostLog.i(TAG_UI, "tempui: full 43284 attack start")
        val lines = java.util.concurrent.ConcurrentLinkedQueue<String>()
        val worker = Thread { lines.addAll(TypedBackendProbe.run(applicationContext)) }
        worker.start()
        worker.join(240_000L)
        GhostLog.i(TAG_UI, "tempui: full 43284 attack done lines=" + lines.size)
        return lines.toList()
    }

    private fun runDeviceGate(): List<String> {
        val typed = TypedBackendProbe.run(applicationContext)

        GhostLog.i(TAG_UI, "tempui: run the device gate")
        return probeNative() + DeviceGate.run() + typed
    }
}

/** 界面的形状与套件无关；控件从 kitOf 取。 */
@Composable
private fun TempUiScreen(
    kit: UiKit,
    onToggleKit: () -> Unit,
    onProbe: () -> List<String>,
    onContracts: () -> List<String>,
    onGate: () -> List<String>,
    onAttack: () -> List<String>,
    onPlan: () -> List<String>,
) {
    val lines = remember { mutableStateListOf<String>() }
    val components = kitOf(kit)

    LaunchedEffect(Unit) {
        GhostLog.configure(LogLevel.Info)
        GhostLog.i(TAG_UI, "tempui: 临时界面已启动")
        lines += "tempui: 临时界面已启动（旧 UI 已废弃）"
        lines += onProbe()
        lines += onGate()
        lines += onPlan()
    }

    Column(modifier = Modifier.fillMaxSize().padding(12.dp)) {
        components.Actions {
            components.Action("界面套件: " + components.label + "（点我切换）") { onToggleKit() }
        }
        components.Actions {
            components.Action("★ 完整攻击（43284：写 patch#1 → 载体 → hook → trigger → wait）") { lines += onAttack() }
        }
        components.Actions {
            components.Action("设备门禁") { lines += onGate() }
            components.Action("跑一遍计划") { lines += onPlan() }
            components.Action("探测 JNI 库") { lines += onProbe() }
            components.Action("能力契约面") { lines += onContracts() }
        }
        components.Actions {
            components.Action("清空") { lines.clear() }
        }
        components.Body(
            text = lines.joinToString("\n"),
            modifier = Modifier.weight(1f).verticalScroll(rememberScrollState()),
        )
    }
}
