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
import androidx.compose.runtime.rememberCoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.ghostlock.app.core.AccessDomain
import com.ghostlock.app.core.AccessFailure
import com.ghostlock.app.core.SessionPhase
import com.ghostlock.app.jni.Native
import com.ghostlock.app.log.GhostLog
import com.ghostlock.app.log.LogLevel
import com.ghostlock.app.log.UnifiedLog
import com.ghostlock.app.data.profile.AndroidAssetSource
import com.ghostlock.app.ui.kit.AndroidUiKitStore
import com.ghostlock.app.ui.kit.UiKit
import com.ghostlock.app.ui.kit.UiKitController
import com.ghostlock.app.ui.kit.kitOf
import com.ghostlock.app.ui.theme.GhostlockTheme

/** 临时界面的 logcat 标签（类与顶层 composable 共用一份）。 */
private const val TAG_UI = "GhostlockUi"

/**
 * 复原按钮的文案（按钮 id 就取这个名字：本界面没有资源 id，UI 测试按它定位）。
 *
 * 它**不是**攻击步骤：只把原厂字节写回载体并还原钩子站点，见 TypedBackendProbe.restoreCarrier。
 */
private const val RESTORE_CARRIER_LABEL = "复原载体 restore carrier"

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
 * 它不宣称任何攻击能力、不发起任何攻击步骤：那些要等设备 接口（J3b/J4b）与 Kotlin 步骤引擎（J6）。
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
                    onAttackStreaming = ::onAttackStreaming,
                    /* 显式带 Context：新进程里 probeContext 还没有值，复原要建 SA 必须自己拿到它。 */
                    onRestoreCarrier = { emit -> TypedBackendProbe.restoreCarrier(applicationContext, emit) },
                    onPlan = ::runPlans,
                )
            }
        }
    }

    /** 探测结果逐行进统一日志（屏幕与 adb 看到同一批行）。 */
    private fun probeNative(): List<String> {
        val loaded = Native.ensureLoaded()
        val lines = listOf(
            "jni: 库名=" + Native.LIBRARY + " 可加载=" + loaded,
            "jni: 入口=create/invoke/shutdown/destroy（四入口；GLKJ 帧 + 会话状态机已落地）",
        )
        lines.forEach { line -> GhostLog.i(TAG_UI, line) }
        return lines
    }

    /** 契约面逐行进统一日志（同上：只记事实）。 */
    private fun reportContracts(): List<String> {
        val lines = listOf(
            "core: AccessDomain=" + AccessDomain.entries.joinToString(),
            "core: AccessFailure=" + AccessFailure.entries.joinToString(),
            "core: SessionPhase=" + SessionPhase.entries.joinToString(),
            "core: 能力接口=KernelMemoryAccess / PageCacheAccess（描述符由 native 发布）",
        )
        lines.forEach { line -> GhostLog.i(TAG_UI, line) }
        return lines
    }

    /** 跑两个 backend 的声明计划；每一步的结局见 logcat 的 GhostlockPlan。 */
    private fun runPlans(): List<String> {
        GhostLog.i(TAG_UI, "tempui: run the declared plans")
        val source = AndroidAssetSource(assets)
        val release = System.getProperty("os.version") ?: "unknown"
        val outcomes = PlanRunner.run(release, source, source)
        return outcomes.map { outcome -> outcome.summary() }
    }

    /**
     *  链的硬前置：SA 未建立就整条跳过（不进入 50 秒写阶段）。
     *
     *  @param emit 逐行回调（与统一日志同内容）。
     *  @return null = SA 就绪、可以跑链；否则为两行拒跑原文（已写进统一日志并交给 emit）。
     */
    private fun saGuard(emit: (String) -> Unit): List<String>? =
        TypedBackendProbe.ensureChainReady(applicationContext) { line ->
            GhostLog.i(TypedBackendProbe.TAG, line)
            emit(line)
        }

    /**
     *  完整攻击（43284）：patch#1 → 载体 → hook → trigger → wait，一次点完。
     *
     *  @note 链子本身在**工作线程**里跑（Android 禁止主线程做网络/重 IO），但这里 **join 回来**，
     *        所以对调用者仍是同步语义 —— 房间里没有留下没被等的异步。
     *  @note 每产生一行就实时回调（界面据此逐行上屏）。
     *  @return 每一步的结局（与 logcat 的 GhostlockTyped 相同）。
     */
    private fun onAttackStreaming(emit: (String) -> Unit): List<String> {
        /* SA 先于链：建不成 ⇒ 两行拒跑，50 秒写阶段一步都不进（21:18 冷跑教训）。 */
        saGuard(emit)?.let { refusal -> return refusal }
        /*
         * 预检短路：KernelSU 已在 ⇒ 整条链会空转（模块已在 /proc/modules，没有可加载目标；
         * 载荷是“一次性”的，且钩子只活在本次开机会话的页缓存里）。
         * 这里直接跳过并说明原因，避免无意义的几十秒等待（真机实测 769 块载体写约 51 s）。
         * 判据只在冷机形态有意义：干净启动后同一会话内才会再出 /dev/dfm0 与 kernelsu。
         */
        val loaded = runCatching {
            java.io.File("/proc/modules").readText().contains("kernelsu")
        }.getOrDefault(false)
        /*
         * 参照实现（ghostlock-app-ref@5f184760）在 **ksu 已加载** 的情况下依旧能跑完并再次启动 ksu，
         * 所以这里**不能在已加载时跳过** ✗ —— 那会挡掉唯一被证明可重复的路径。
         * 改为：只**提示**当前状态，然后照常跑链（是否成功由链自己的判据决定）。
         */
        if (loaded) {
            emit("note: kernelsu already loaded -> running anyway (the reference does the same)")
            emit("kernelsu: " + ksuState())
        }
        return runAttackStreamingInner(emit)
    }

    /** 已加载状态的一句话摘要（模块行与三标记态），供预检与面板复用。 */
    private fun ksuState(): String {
        val module = runCatching {
            java.io.File("/proc/modules").readText().lineSequence().firstOrNull { it.contains("kernelsu") }
        }.getOrNull() ?: "(absent)"
        val marks = listOf("/dev/df", "/dev/dfm0", "/dev/dfm1").joinToString(" ") { path ->
            path + "=" + (if (java.io.File(path).exists()) "yes" else "no")
        }
        return module.trim() + " | " + marks
    }

    private fun runAttackStreamingInner(emit: (String) -> Unit): List<String> {
        saGuard(emit)?.let { refusal -> return refusal }
        GhostLog.i(TAG_UI, "tempui: full 43284 attack start (streaming)")
        return TypedBackendProbe.run(applicationContext, emit)
    }

    private fun runAttack(): List<String> {
        saGuard { }?.let { refusal -> return refusal }
        GhostLog.i(TAG_UI, "tempui: full 43284 attack start")
        val lines = java.util.concurrent.ConcurrentLinkedQueue<String>()
        lines.addAll(TypedBackendProbe.run(applicationContext))
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
    onAttackStreaming: ((String) -> Unit) -> List<String>,
    onRestoreCarrier: suspend ((String) -> Unit) -> Boolean,
    onPlan: () -> List<String>,
) {
    val lines = remember { mutableStateListOf<String>() }
    val components = kitOf(kit)
    val scope = rememberCoroutineScope()
    /* 统一日志流是否可用（日志 fd 打开成功）：可用时主显示是它，否则退回下面的旧列表。 */
    var unified by remember { mutableStateOf(false) }

    /* 重活一律走 IO 线程：界面立刻可点（此前主线程被占住 ⇒ 按钮全无响应），且异常变成一行文字而不是崩溃。 */
    /* 流式版本：动作每产生一行就上屏（攻击链要跑半分钟以上，不能等它结束才显示）。 */
    fun runStreaming(what: String, action: (((String) -> Unit)) -> List<String>) {
        scope.launch {
            val collected = mutableListOf<String>()
            val out = runCatching {
                withContext(Dispatchers.IO) {
                    action { line -> collected += line; lines += line }
                }
            }.getOrElse { problem ->
                /* 失败也是一行日志：进统一流，屏幕上看得见（不只落在下面的兜底列表里）。 */
                val report = what + " 失败: " + problem.javaClass.simpleName + ": " + (problem.message ?: "")
                GhostLog.e(TAG_UI, report)
                listOf(report)
            }
            if (out.isNotEmpty() && collected.isEmpty()) {
                lines += out
            }
        }
    }

    fun runHeavy(what: String, action: () -> List<String>) {
        scope.launch {
            val out = runCatching { withContext(Dispatchers.IO) { action() } }
                .getOrElse { problem ->
                    /* 同上：失败行也进统一流。 */
                    val report = what + " 失败: " + problem.javaClass.simpleName + ": " + (problem.message ?: "")
                    GhostLog.e(TAG_UI, report)
                    listOf(report)
                }
            lines += out
        }
    }

    /**
     * 复原载体：**只跑** TypedBackendProbe.restoreCarrier（写回原厂字节 + 还原钩子站点）。
     *
     * 与攻击分开的入口：不建链、不加载模块、不 trigger。日志由 restoreCarrier 自己写 GhostLog
     * （统一面板读的是同一份文件），这里只把同一批行收进旧列表兜底，并在最后补一行结果。
     */
    fun runRestoreCarrier() {
        scope.launch {
            val ok = runCatching {
                withContext(Dispatchers.IO) {
                    onRestoreCarrier { line ->
                        lines += line
                    }
                }
            }.getOrElse { problem ->
                val report = "复原载体 失败: " + problem.javaClass.simpleName + ": " + (problem.message ?: "")
                GhostLog.e(TAG_UI, report)
                lines += report
                false
            }
            val report = "restore: carrier restored=" + ok
            GhostLog.i(TypedBackendProbe.TAG, report)
            lines += report
        }
    }

    LaunchedEffect(Unit) {
        GhostLog.configure(LogLevel.Info)
        /* 统一日志流（2026-10-16 用户裁定：屏幕上的日志必须与 adb 的一样）：打开文件 + 把 native
         * 三个库接到同一个 fd 上；状态行本身也写进流里，屏幕上看得到"接上了没有"。 */
        UnifiedLog.start().forEach { statusLine -> GhostLog.i(TAG_UI, statusLine) }
        unified = UnifiedLog.fileReady
        GhostLog.i(TAG_UI, "tempui: 临时界面已启动")
        lines += "tempui: 临时界面已启动（旧 UI 已废弃）"
        /* 用户裁定（2026-10-16）：**启动即执行**一律取消 —— 整条链只由按钮触发（否则一开 app 就跑攻击）。 */
    }

    Column(modifier = Modifier.fillMaxSize().padding(12.dp)) {
        components.Actions {
            components.Action("界面套件: " + components.label + "（点我切换）") { onToggleKit() }
        }
        components.Actions {
            components.Action("★ 完整攻击（43284：写 patch#1 → 载体 → hook → trigger → wait）") { runStreaming("攻击") { emit -> onAttackStreaming(emit) } }
        }
        components.Actions {
            components.Action("设备门禁") { runHeavy("门禁") { onGate() } }
            components.Action("跑一遍计划") { runHeavy("计划") { onPlan() } }
            components.Action("探测 JNI 库") { runHeavy("探测") { onProbe() } }
            components.Action("能力契约面") { runHeavy("契约") { onContracts() } }
        }
        /* 复原入口：**单独一排**，与上面的攻击按钮（★ 那一行）隔开。
         * 它不是攻击步骤：只把 /system 的原厂前 23512 B 写回 /vendor 载体，并还原钩子站点。
         * 点它只跑 TypedBackendProbe.restoreCarrier。 */
        components.Body("复原 = 补救路径（写回原厂字节；不加载模块、不 trigger）", Modifier)
        components.Actions {
            components.Action(RESTORE_CARRIER_LABEL) { runRestoreCarrier() }
        }
        components.Actions {
            components.Action("清空") {
                lines.clear()
                /* 屏幕上的那一份也一起清（两处一起清，屏幕与 adb 才不会各显示一半）。 */
                UnifiedLog.clear()
            }
        }
        if (unified) {
            /* 主显示：统一日志流（Kotlin 行 + native 行，同一份文件；尾部跟随 + 自动滚动）。 */
            LogStreamPanel(kit = components, modifier = Modifier.weight(1f))
        } else {
            /* //TODO(unified-log-20261016): 旧显示（行动返回的原始列表）留作兜底：日志 fd 打不开时
             * 它是唯一还能看的去处；统一流稳定后与 FileSink 的旧兜底一起清理。 */
            components.Body(
                text = lines.joinToString("\n"),
                modifier = Modifier.weight(1f).verticalScroll(rememberScrollState()),
            )
        }
    }
}
