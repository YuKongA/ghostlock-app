package com.ghostlock.app.ui.backend

import androidx.compose.runtime.Immutable

/**
 * backend 的**参数/偏移声明**（只声明元数据；值仍由 profile/debug 配置提供）。
 *
 * 用途（用户裁定 2026-10-28）：
 * - 每个 backend 在 `BackendSpec` 声明自己需要哪些内核偏移/地址/尺寸/调参项；
 * - UI/配置页据此展示与做静态校验（后端不自检运行时环境）；
 * - Lua 等动态 backend 可由脚本元数据生成同一 schema；
 * - 公共 ABI 字段（task_struct/cred 布局）与 backend 私有项分开，便于多个 backend 复用同一套 ABI 声明。
 *
 * @property name 稳定 snake_case 键（与 profile/参数串键一致）。
 * @property kind 值类型。
 * @property scope 复用范围。
 * @property source 值来源。
 * @property required 是否必需；false=可选/待迁移。
 * @property description 语义说明（中文/英文均可；不写具体目标数值）。
 * @note 这里只声明，不含真实内核数值；数值必须在 profile/debug 配置里。
 */
@Immutable
data class BackendParameterDecl(
    val name: String,
    val kind: BackendParameterKind,
    val scope: BackendParameterScope,
    val source: BackendParameterSource,
    val required: Boolean = true,
    val description: String = "",
) {
    init {
        require(name.matches(NAME_REGEX)) { "backend parameter must be snake_case: " + name }
        require(name.length <= MAX_NAME_LENGTH) { "backend parameter name too long: " + name }
    }

    private companion object {
        private val NAME_REGEX = Regex("[a-z][a-z0-9_]*")
        private const val MAX_NAME_LENGTH = 64
    }
}

/** 参数值类型（分类用；不在此承载值）。 */
enum class BackendParameterKind {
    /** 无符号整数/位模式（u64）。 */
    U64,

    /** 内核虚拟地址/符号地址（运行时加 slide）。 */
    ADDRESS,

    /** 结构体字段偏移。 */
    OFFSET,

    /** 尺寸/长度（bytes）。 */
    SIZE,

    /** 能力位掩码/SID/标志值。 */
    MASK,

    /** CPU 编号。 */
    CPU,

    /** 字符串（包名/KMI/路径）。 */
    STRING,

    /** token 列表（late-load args/回退路径）。 */
    TOKEN_LIST,
}

/** 复用范围。 */
enum class BackendParameterScope {
    /** 公共内核 ABI（task_struct/cred 布局；多个 backend 可共用声明）。 */
    COMMON_KERNEL_ABI,

    /** 该 backend 的每构建 profile 参数。 */
    BACKEND_PROFILE,

    /** 算法/时序调参（不随内核变化）。 */
    BACKEND_TUNING,
}

/** 值来源（决定谁提供、UI 如何校验）。 */
enum class BackendParameterSource {
    /** 由 backend profile/debug 配置提供（用户/构建维护）。 */
    PROFILE,

    /** 目前硬编码在 native/Kotlin，应提升为 profile（声明的 TODO）。 */
    COMPILE_CONSTANT,

    /** 运行时发现（如 trace event id、KASLR slide、线性映射基址）。 */
    RUNTIME_DISCOVERED,
}

/**
 * 一个 backend 的完整参数声明。
 *
 * @property parameters 声明项（名称唯一、总数 ≤ [MAX_PARAMETERS]）。
 */
@Immutable
data class BackendParameterSchema(
    val parameters: List<BackendParameterDecl> = emptyList(),
) {
    init {
        require(parameters.size <= MAX_PARAMETERS) { "too many backend parameters" }
        require(parameters.map { it.name }.toSet().size == parameters.size) {
            "duplicate backend parameter"
        }
    }

    /** @return 名为 [name] 的声明；未声明返回 null。 */
    fun parameter(name: String): BackendParameterDecl? =
        parameters.firstOrNull { it.name == name }

    /** @return 按 scope 过滤。 */
    fun scope(scope: BackendParameterScope): List<BackendParameterDecl> =
        parameters.filter { it.scope == scope }

    companion object {
        /** 单个 backend 最多声明项数（防御性上限）。 */
        private const val MAX_PARAMETERS = 256

        /** 空声明（未声明任何参数）。 */
        val EMPTY: BackendParameterSchema = BackendParameterSchema()
    }
}
