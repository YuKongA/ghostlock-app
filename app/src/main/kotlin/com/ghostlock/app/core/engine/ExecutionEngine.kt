package com.ghostlock.app.core.engine

import com.ghostlock.app.core.model.DeviceInfo
import com.ghostlock.app.core.model.Plan
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 *  执行状态机（2026-10-18 框架）：Idle → Checking → RunningPath → Finishing → Success/Failed。
 *
 *  界面只订阅它；开机自启时由后台直接驱动 engine，不经过 UI。
 */
sealed interface ExecutionState {
    data object Idle : ExecutionState

    /** 能力探测与校验。 */
    data class Checking(val note: String) : ExecutionState

    /** 正在跑路径。@property step 当前步骤名。 */
    data class RunningPath(val step: String, val progress: Float) : ExecutionState

    /** 收尾流程。 */
    data class Finishing(val step: String) : ExecutionState

    data class Success(val note: String) : ExecutionState

    data class Failed(val reason: String) : ExecutionState
}

/** 能力探测：启动时一次，结果缓存为 [DeviceInfo]（框架阶段用 [StaticProbe]）。 */
interface DeviceProbe {
    suspend fun probe(): DeviceInfo
}

/** 一个已经探测好的设备事实（框架阶段代替真探测）。 */
class StaticProbe(private val device: DeviceInfo) : DeviceProbe {
    override suspend fun probe(): DeviceInfo = device
}

/** 执行引擎：界面与后台共用同一个入口。一次性使用，必须 close。 */
interface ExecutionEngine : AutoCloseable {
    /** 当前状态（界面订阅）。 */
    val state: StateFlow<ExecutionState>

    /** 跑一个方案；实现负责把状态推进到 Success/Failed。 */
    suspend fun run(plan: Plan)

    /** 幂等；拒绝并发 run；close 后 run 必须失败。 */
    override fun close()
}

/**
 *  框架阶段的**假**引擎：只按固定节奏推进状态与日志，不调用任何 backend。
 *
 *  @param steps 假步骤名。
 *  @param onLog 每一行日志的去处。
 */
class ScriptedEngine(
    private val steps: List<String> = listOf("检查设备", "建立会话", "执行路径", "收尾"),
    private val onLog: (String) -> Unit = {},
) : ExecutionEngine {
    private val _state = MutableStateFlow<ExecutionState>(ExecutionState.Idle)
    override val state: StateFlow<ExecutionState> = _state.asStateFlow()
    private var closed = false

    override suspend fun run(plan: Plan) {
        check(!closed) { "engine closed" }
        _state.value = ExecutionState.Checking("probe")
        onLog("engine: checking " + plan.path.id)
        val problems = plan.validate()
        if (problems.isNotEmpty()) {
            _state.value = ExecutionState.Failed(problems.joinToString("；"))
            onLog("engine: failed " + problems.joinToString("；"))
            return
        }
        steps.forEachIndexed { index, step ->
            _state.value = ExecutionState.RunningPath(step, (index + 1f) / steps.size.toFloat())
            onLog("engine: " + step)
        }
        _state.value = ExecutionState.Finishing(plan.finisher.title)
        onLog("engine: finishing " + plan.finisher.id)
        _state.value = ExecutionState.Success("完成（框架阶段：未接线）")
        onLog("engine: success")
    }

    override fun close() {
        if (closed) return
        closed = true
        _state.value = ExecutionState.Failed("engine-closed")
    }
}

/** Shell 执行面（框架阶段只有声明；普通 / Shizuku / Root 三种实现将来接）。 */
interface ShellRunner {
    suspend fun exec(command: String): Int
}
