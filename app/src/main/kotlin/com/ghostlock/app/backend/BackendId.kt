package com.ghostlock.app.backend

import com.ghostlock.app.backend.cve43284.flow.Flow43284Impl
import com.ghostlock.app.backend.cve43499.flow.Flow43499Impl
import com.ghostlock.app.backend.lua.FlowLuaImpl
import com.ghostlock.app.core.flow.Flow
import kotlin.reflect.KClass

/**
 * backend 注册表。
 *
 * 身份是实现类本身；不保存 wire string。
 * 该 enum 位于 backend 层，避免 core 反向依赖具体实现。
 */
enum class BackendId(val clazz: KClass<out Flow>) {
    CVE_43284(Flow43284Impl::class),
    CVE_43499(Flow43499Impl::class),
    LUA(FlowLuaImpl::class),
}
