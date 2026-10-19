package com.ghostlock.app.core.model

/**
 *  配置文件的声明（由路径自己给）：格式 + 必填字段。
 *
 *  UI 只展示三种状态（[ConfigState]）；解析与校验在将来的 engine 里，不在界面里。
 *
 *  @property format 展示用的格式名（如 "GLKv3 / GLKJ"）。
 *  @property requiredFields 必填字段的点分名（如 "sections.execution.settle_us"）。
 */
data class ConfigSpec(
    val format: String,
    val requiredFields: List<String> = emptyList(),
)

/** 一份已加载的配置（真实解析结果将来由 engine 产出；框架阶段只有名字与字节数）。 */
data class LoadedConfig(
    val name: String,
    val bytes: Int,
    val fields: Map<String, String> = emptyMap(),
)

/** 配置在界面上的三种状态。 */
sealed interface ConfigState {
    /** 已加载并通过校验。 */
    data class Loaded(val config: LoadedConfig) : ConfigState

    /** 还没选文件。 */
    data object Missing : ConfigState

    /** 选过但校验失败（带错误行，界面逐行显示）。 */
    data class Invalid(val errors: List<String>) : ConfigState
}
