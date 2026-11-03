package com.ghostlock.app.backend.declarations

import com.ghostlock.app.BuildConfig
import com.ghostlock.app.backend.registry.Backends
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/**
 * `@UnfinishedBackend` 不变量（2026-11-03 用户裁定：统一 `backend/backends/`，未完工用注解标记）。
 *
 * 覆盖：
 * - builtin 中 `selectable=false` ⇒ 必须带 [UnfinishedBackend]（未完工不得“裸奔不可选”）；
 * - 带 [UnfinishedBackend] ⇒ 必须 `selectable=false`（注解只标记未完工，不授予可选性）；
 * - 注解 `RUNTIME` 可见、`reason` 非空、且只标记 43499/43284/Lua 这 3 个未完工 backend；
 * - debug probe 不带注解（debug-only 可运行，不是未完工）。
 *
 * @note 本测试只用内置 registry：每个用例前后 [Backends.resetToBuiltins]，隔离动态注册。
 */
class UnfinishedBackendAnnotationTest {
    @Before
    fun setUp() {
        Backends.resetToBuiltins()
    }

    @After
    fun tearDown() {
        Backends.resetToBuiltins()
    }

    /** @return 按 `RUNTIME` 注解读取的未完工标记；未标注返回 null。 */
    private fun unfinishedOf(spec: BackendSpec): UnfinishedBackend? =
        spec.javaClass.getAnnotation(UnfinishedBackend::class.java)

    @Test
    fun builtinNonSelectableSpecsAreAnnotated() {
        val unmarked = Backends.all.filter { !it.selectable && unfinishedOf(it) == null }
        assertTrue(
            "builtin 中 selectable=false 必须带 @UnfinishedBackend：" + unmarked.map { it.key },
            unmarked.isEmpty(),
        )
    }

    @Test
    fun annotatedSpecsAreNotSelectable() {
        val bad = Backends.all.filter { it.selectable && unfinishedOf(it) != null }
        assertTrue(
            "带 @UnfinishedBackend 的 spec 不得 selectable=true：" + bad.map { it.key },
            bad.isEmpty(),
        )
    }

    @Test
    fun annotationIsRuntimeVisibleWithNonBlankReason() {
        val annotated = Backends.all.mapNotNull { spec -> unfinishedOf(spec)?.let { spec to it } }
        assertEquals("内置未完工 backend 数量（仅 43499/43284/Lua）", 3, annotated.size)
        assertEquals(
            "未完工 backend key 集合",
            setOf("CVE-2026-43499", "CVE-2026-43284", "CUSTOM"),
            annotated.map { it.first.key }.toSet(),
        )
        annotated.forEach { (spec, annotation) ->
            assertTrue("reason 不得为空：" + spec.key, annotation.reason.isNotBlank())
        }
    }

    @Test
    fun debugProbeIsNotAnnotated() {
        val probe = Backends.byKey("DEBUG-PROBE")
        assertEquals("probe 注册应与 BuildConfig.DEBUG 一致", BuildConfig.DEBUG, probe != null)
        if (probe != null) {
            assertTrue("debug probe 是 debug-only 可运行 backend，应 selectable", probe.selectable)
            assertNull("debug probe 不得带 @UnfinishedBackend", unfinishedOf(probe))
        }
    }
}
