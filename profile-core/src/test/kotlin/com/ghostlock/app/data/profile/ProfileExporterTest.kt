package com.ghostlock.app.data.profile

import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class ProfileExporterTest {
    private val src = File("/repo/app/src/main/assets/profile")
    private val build = File("/repo/build")
    private val expected = File("/repo/build/profiles")

    @Test
    fun `accepts the configured export dir`() {
        ProfileExporter.validateOutputDir(src, expected, expected, build)
    }

    @Test
    fun `rejects an output dir outside the given build dir`() {
        /* The safety property is CONTAINMENT: a configured dir that still sits
         * outside the build tree the run was given stays refused. */
        val outside = File("/private/tmp/evil")
        val failure = assertThrows(IllegalArgumentException::class.java) {
            ProfileExporter.validateOutputDir(src, outside, outside, build)
        }
        println("REFUSAL-OUTSIDE: " + failure.message)
        assertTrue(
            "the refusal must name the build dir: " + failure.message,
            failure.message!!.contains("must live under the build directory"),
        )
    }

    @Test
    fun `rejects a build dir that is only a name match`() {
        /* A path merely CONTAINING a build segment is not inside OUR build tree. */
        val unrelated = File("/private/tmp/build/user-data")
        assertThrows(IllegalArgumentException::class.java) {
            ProfileExporter.validateOutputDir(src, unrelated, unrelated, build)
        }
    }

    @Test
    fun `accepts an export dir nested anywhere inside the build tree`() {
        /* 沿革: 曾以 build.nosync 符号链接情境表述（"/repo/build.nosync" 作为 relocated 树，
         * 其路径不含 build 段）。2026-10-06 用户规则改为【所有构建必须在仓库内真实 build/，
         * 禁止任何链接】，故改用真实 build/ 树内的嵌套路径表达同一【包含性】语义：
         * 只要落在给定的 build 树内就被接受（与名字是否叫 build 无关）。
         * 反例仍由 `rejects an output dir outside the given build dir` 与
         * `rejects a build dir that is only a name match` 两例覆盖（零放宽）。 */
        val nested = File("/repo/build/profiles/nested")
        assertTrue("the nested export dir must live inside the build tree", nested.absolutePath.startsWith(build.absolutePath + File.separator))
        ProfileExporter.validateOutputDir(src, nested, nested, build)
    }

    @Test
    fun `rejects the source tree`() {
        val inSource = File("/repo/app/src/main/assets")
        assertThrows(IllegalArgumentException::class.java) {
            ProfileExporter.validateOutputDir(src, inSource, inSource, build)
        }
    }

    @Test
    fun `rejects the profiles dir itself`() {
        assertThrows(IllegalArgumentException::class.java) {
            ProfileExporter.validateOutputDir(src, src, src, build)
        }
    }
}
