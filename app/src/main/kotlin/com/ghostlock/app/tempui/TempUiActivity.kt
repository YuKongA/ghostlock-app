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
import com.ghostlock.app.ui.kit.AndroidUiKitStore
import com.ghostlock.app.ui.kit.UiKit
import com.ghostlock.app.ui.kit.UiKitController
import com.ghostlock.app.ui.kit.kitOf
import com.ghostlock.app.ui.theme.GhostlockTheme

/**
 * 最小临时 UI：只做诊断，不做业务。
 *
 * 用户裁定（2026-10-14）：旧 Kotlin UI 全面废弃，测试期间只保留一个临时界面；**新 UI 用 Material，
 * 并像 KernelSU 那样可以灵活切到 Miuix**。所以这个界面本身就是那套切换的第一个用户：
 * 顶部一个按钮在两个套件之间切换（选择落 SharedPreferences，切完立刻重绘），下面的诊断按钮
 * 在任何一个套件下都能用 —— 因为界面只依赖 [com.ghostlock.app.ui.kit.GhostlockKit] 的语义。
 *
 * 它**不**宣称任何攻击能力、不发起任何攻击步骤：那些要等对应批次的 native 入口与 Kotlin
 * 步骤引擎落地（见 docs/analysis/jni-two-backend-plan.md）。
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
}

/** 界面的形状与套件无关；控件从 [kitOf] 取。 */
@Composable
private fun TempUiScreen(
    kit: UiKit,
    onToggleKit: () -> Unit,
    onProbe: () -> List<String>,
    onContracts: () -> List<String>,
) {
    val lines = remember { mutableStateListOf<String>() }
    val components = kitOf(kit)

    LaunchedEffect(Unit) {
        lines += "tempui: 临时界面已启动（旧 UI 已废弃）"
        lines += onProbe()
    }

    Column(modifier = Modifier.fillMaxSize().padding(12.dp)) {
        components.Actions {
            components.Action("界面套件: " + components.label + "（点我切换）") { onToggleKit() }
        }
        components.Actions {
            components.Action("探测 JNI 库") { lines += onProbe() }
            components.Action("能力契约面") { lines += onContracts() }
            components.Action("清空") { lines.clear() }
        }
        components.Body(
            text = lines.joinToString("\n"),
            modifier = Modifier.weight(1f).verticalScroll(rememberScrollState()),
        )
    }
}
