package com.ghostlock.app.engine.pagecache

import java.io.File

/** 测试 ELF 夹具：优先仓库内 test resources，其次 /tmp 本机副本。 */
object FixtureFiles {
    fun emulibcxx(): File = find("emulibcxx.so")

    fun crashDump64(): File = find("crash_dump64")

    private fun find(name: String): File {
        val candidates = listOf(
            File("app/src/test/resources/fixtures/" + name),
            File("src/test/resources/fixtures/" + name),
            File("/tmp/" + name),
        )
        return candidates.firstOrNull { it.isFile }
            ?: error("missing test fixture: " + name + " (checked " + candidates + ")")
    }
}
