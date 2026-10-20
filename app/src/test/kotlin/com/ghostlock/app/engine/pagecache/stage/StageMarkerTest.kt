package com.ghostlock.app.engine.pagecache.stage

import java.io.File
import org.junit.Assert.assertTrue
import org.junit.Test

/** B6：新 Stage/纯库不得重新引入旧 signal marker。 */
class StageMarkerTest {
    private val roots = listOf(
        File("app/src/main/kotlin/com/ghostlock/app/engine/pagecache/stage"),
        File("core/src/backend/cve_2026_43284/lib"),
    )
    private val forbidden = listOf("/data/local/tmp/.gl_", "ghostlock_lkm_", "ghostlock-lkm-ready")

    @Test
    fun newStageAndPureLibHaveNoLegacyMarkers() {
        val hits = mutableListOf<String>()
        for (root in roots) {
            if (!root.isDirectory) continue
            root.walkTopDown().filter { it.isFile && it.extension in setOf("kt", "hpp", "cpp") }
                .forEach { file ->
                    file.readLines().forEachIndexed { index, line ->
                        forbidden.firstOrNull { line.contains(it) }?.let {
                            hits += file.path + ":" + (index + 1) + ":" + it
                        }
                    }
                }
        }
        assertTrue("旧 marker 进入新代码：" + hits.joinToString(), hits.isEmpty())
    }
}
