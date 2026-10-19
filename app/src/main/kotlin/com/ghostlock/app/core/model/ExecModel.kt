package com.ghostlock.app.core.model

/** 一条执行路径能拿到的权限：决定它能开哪扇门。 */
enum class Privilege { NONE, SHIZUKU, ROOT }

/** 路径在当前设备上的可用性：不支持时必须带原因（UI 灰显 + 写明为什么）。 */
sealed interface Support {
    data class Supported(val note: String = "") : Support
    data class Unsupported(val reason: String) : Support
}

/**
 *  设备事实（启动时探测一次并缓存）。
 *
 *  @property soc 处理器型号（如 SM8750）。
 *  @property kernel 内核版本串。
 *  @property android 安卓版本。
 *  @property cores CPU 核心数（多选类选项用它生成候选项）。
 *  @property root 是否已有 root。
 *  @property shizuku Shizuku 是否可用且已授权。
 */
data class DeviceInfo(
    val soc: String,
    val kernel: String,
    val android: String,
    val cores: Int,
    val root: Boolean,
    val shizuku: Boolean,
)

/** 路径产出的东西：交给收尾流程消费（具体形状由路径自己定）。 */
interface PathOutput {
    val pathId: String
}

/** 一次运行共享的上下文：设备事实、选中的选项、日志出口。 */
interface RunContext {
    val device: DeviceInfo
    val values: Map<String, Any>
    fun log(line: String)
}

/** 一个用户可配置的选项。UI 只认这些形状，不认具体页面。 */
sealed interface OptionSpec {
    val key: String
    val title: String
    val summary: String?

    data class Toggle(
        override val key: String,
        override val title: String,
        override val summary: String? = null,
        val default: Boolean = false,
    ) : OptionSpec

    data class Single(
        override val key: String,
        override val title: String,
        override val summary: String? = null,
        val items: List<String>,
        val defaultIndex: Int = 0,
    ) : OptionSpec

    data class Multi(
        override val key: String,
        override val title: String,
        override val summary: String? = null,
        val items: List<String>,
        val defaultSelected: Set<Int> = emptySet(),
    ) : OptionSpec

    data class IntRange(
        override val key: String,
        override val title: String,
        override val summary: String? = null,
        val min: Int,
        val max: Int,
        val default: Int,
        val step: Int = 1,
    ) : OptionSpec

    data class Text(
        override val key: String,
        override val title: String,
        override val summary: String? = null,
        val default: String = "",
    ) : OptionSpec
}

/** 一条执行路径：只声明自己需要什么，不画任何 UI。 */
interface ExecPath {
    val id: String
    val title: String
    val privilege: Privilege
    fun probe(device: DeviceInfo): Support
    fun options(device: DeviceInfo): List<OptionSpec> = emptyList()
    suspend fun run(context: RunContext): PathOutput
}

/** 收尾流程：拿路径的产出做最后一步（装载、复位、清理……）。 */
interface Finisher {
    val id: String
    val title: String
    fun options(device: DeviceInfo): List<OptionSpec> = emptyList()
    fun accepts(output: PathOutput): Boolean = true
    suspend fun finish(output: PathOutput, context: RunContext)
}

/** 一次执行计划：路径 + 收尾 + 选项值。 */
data class Plan(
    val path: ExecPath,
    val finisher: Finisher,
    val values: Map<String, Any> = emptyMap(),
) {
    /** 执行前的静态校验；空表表示可以跑。 */
    fun validate(): List<String> {
        val problems = mutableListOf<String>()
        if (!finisher.accepts(DummyOutput(path.id))) {
            problems += "收尾流程不接受这条路径的产出"
        }
        return problems
    }

    private data class DummyOutput(override val pathId: String) : PathOutput
}
