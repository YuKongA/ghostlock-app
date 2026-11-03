package com.ghostlock.app.backend.registry

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.ghostlock.app.BuildConfig
import com.ghostlock.app.backend.backends.cve43284glued.BackendCve43284Glued
import com.ghostlock.app.backend.backends.cve43499glued.BackendCve43499Glued
import com.ghostlock.app.backend.backends.debugprobe.BackendDebugProbe
import com.ghostlock.app.backend.backends.lua.BackendCustom
import com.ghostlock.app.backend.backends.cve43284.BackendCve43284
import com.ghostlock.app.backend.backends.cve43499.BackendCve43499
import com.ghostlock.app.backend.declarations.BackendSelection
import com.ghostlock.app.backend.declarations.BackendSpec

object Backends {
    private val builtins: List<BackendSpec> = buildList {
        add(BackendCve43499Glued)
        add(BackendCve43284Glued)
        add(BackendCve43284)
        add(BackendCve43499)
        add(BackendCustom)
        /* debug probe 仅 debug 构建注册；release 菜单/registry 均不含。 */
        if (BuildConfig.DEBUG) add(BackendDebugProbe)
    }

    /** 当前全部 backend（内置 + 动态）；顺序=下拉框顺序。 */
    var all: List<BackendSpec> by mutableStateOf(builtins)
        private set

    /**
     * 选择权威：**只有** [BackendSpec.selectable]（注册表无对象身份白名单）。
     *
     * @param spec 已注册 backend。
     * @return 是否可在 UI 选择/运行。
     * @note debug probe 的 selectable=BuildConfig.DEBUG；release 不在 builtins。
     */
    fun isSelectable(spec: BackendSpec): Boolean = spec.selectable

    /**
     * 注册一个动态 backend（如同一个 Lua 脚本）。
     *
     * @param spec backend 声明。
     * @return true=注册成功；key 冲突返回 false（不覆盖）。
     */
    fun register(spec: BackendSpec): Boolean {
        /* 运行时 fail-closed：空/重复 key、selectable 但无 flow 入口一律拒绝且不改 all。 */
        if (spec.key.isBlank()) return false
        if (all.any { it.key == spec.key }) return false
        if (spec.selectable && spec.flowSymbol == null) return false
        all = all + spec
        return true
    }

    /**
     * 注销动态 backend（内置不可注销）。
     *
     * @param key 稳定 key。
     * @return true=已移除。
     */
    fun unregister(key: String): Boolean {
        if (builtins.any { it.key == key }) return false
        val next = all.filterNot { it.key == key }
        if (next.size == all.size) return false
        all = next
        if (BackendSelection.selectedKey == key) {
            BackendSelection.selectedKey = next.firstOrNull { isSelectable(it) }?.key
        }
        return true
    }

    /** @return 稳定 key 对应的 spec；不存在 null。 */
    fun byKey(key: String?): BackendSpec? = all.firstOrNull { it.key == key }

    /** 恢复内置列表（测试/诊断用）。 */
    fun resetToBuiltins() {
        all = builtins
        if (BackendSelection.selectedKey?.let { key ->
                builtins.any { it.key == key && isSelectable(it) }
            } != true
        ) {
            BackendSelection.selectedKey = builtins.firstOrNull { isSelectable(it) }?.key
        }
    }
}
