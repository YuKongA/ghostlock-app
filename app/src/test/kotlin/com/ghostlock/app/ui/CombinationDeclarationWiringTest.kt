package com.ghostlock.app.ui

import com.ghostlock.app.data.DeclaredCombination
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * 接线层缺口的守卫（补 ExecutionCombinationProjectionTest 从不触碰的那一段）。
 *
 * 现有投影测试的输入全是【合成的 List<ExecutionComboEntry>】⇒ 只测「给定正确 entries 后菜单怎么排」；
 * 而用户报的缺陷发生在【声明 → 默认项 → entries → 失败可见性】这条接线：那里此前零覆盖。
 */
class CombinationDeclarationWiringTest {
    private val vocabulary = ExecutionComboVocabulary(
        backends = setOf("cve_2026_43499", "cve_2026_43284"),
        routes = setOf("multicast_waiter", "select_stack", "tcp_zerocopy"),
        terminals = setOf("root_child", "shizuku", "umh_forward"),
        steps = setOf("w1", "w2", "w3"),
    )

    private fun declared(
        backend: String,
        route: String?,
        steps: List<String>,
    ) = DeclaredCombination(backend = backend, route = route, steps = steps, handoff = null, priority = 1L)

    /* 3a 仅声明 43284（无 route 轴）⇒ 默认项必须【由声明派生】为 43284，而不是目录默认 43499。 */
    @Test
    fun theDeclarationDecidesTheDefaultNotTheCatalogue() {
        val spec = declaredDefaultCombination(
            listOf(declared("cve_2026_43284", null, listOf("pagecache_write"))),
        )
        assertEquals("the declared default must win over the catalogue default", "cve_2026_43284", spec?.backend?.token)
    }

    /* 3b 换一份声明（首项 43499 + multicast_waiter）⇒ 结果必须随之变化（重新派生，不锁定）。 */
    @Test
    fun changingTheDeclarationReDerivesTheDefault() {
        val first = declaredDefaultCombination(
            listOf(declared("cve_2026_43284", null, listOf("pagecache_write"))),
        )
        val second = declaredDefaultCombination(
            listOf(declared("cve_2026_43499", "multicast_waiter", listOf("w1", "w2", "w3"))),
        )
        assertEquals("cve_2026_43284", first?.backend?.token)
        assertEquals("cve_2026_43499", second?.backend?.token)
        assertTrue("the two declarations must not derive the same default", first?.backend?.token != second?.backend?.token)
    }

    /* 3c 声明存在但目录无匹配 ⇒ 必须返回 null（不得静默回落目录默认）。 */
    @Test
    fun anUnmatchedDeclarationNeverFallsBackSilently() {
        val spec = declaredDefaultCombination(
            listOf(declared("cve_2026_43499", "not_a_route", listOf("w1"))),
        )
        assertNull("an unmatched declaration must yield no default, never a catalogue fallback", spec)
    }

    /* 1b 「有声明但派生不出可用项」：空步骤序列必须进入 failures，不得静默丢弃。 */
    @Test
    fun unusableDeclarationsAreFailVisibleNeverSilent() {
        val unusable = ExecutionComboEntry(
            backend = "cve_2026_43284",
            route = null,
            steps = emptyList(),
            terminal = "umh_forward",
            priority = 1,
        )
        val tree = executionComboTree(listOf(unusable), vocabulary)
        assertTrue(
            "an empty step sequence must be reported, not silently dropped: " + tree.failures,
            tree.failures.any { it.contains("empty step sequence") },
        )
    }

    /* 1b 第二形态：未知 route 必须被具名上报（不得当作可用项渲染）。 */
    @Test
    fun unknownDeclaredRouteIsFailVisible() {
        val unknown = ExecutionComboEntry(
            backend = "cve_2026_43499",
            route = "not_a_route",
            steps = listOf("w1"),
            terminal = "root_child",
            priority = 1,
        )
        val tree = executionComboTree(listOf(unknown), vocabulary)
        assertTrue(
            "unknown route must be reported: " + tree.failures,
            tree.failures.any { it.contains("unknown route") },
        )
    }

    /* 用户症状「突兀说明」：该键必须已从【两份】strings.xml 删除。 */
    @Test
    fun theComboOrderNoteKeyIsGoneFromBothStringResources() {
        /* CWD = 模块目录 app/（本仓惯例，如 StepQueueAssetMigrationTest 用 src/test/resources/…）
         * ⇒ 路径必须【模块相对】，写 "app/src/main/…" 会变成 app/app/src/… ⇒ FileNotFound。 */
        val files = listOf(
            "src/main/res/values/strings.xml",
            "src/main/res/values-zh/strings.xml",
        )
        for (path in files) {
            val text = File(path).readText()
            assertTrue(
                path + " must not declare execution_combo_order_note any more",
                !text.contains("execution_combo_order_note"),
            )
        }
    }
}
