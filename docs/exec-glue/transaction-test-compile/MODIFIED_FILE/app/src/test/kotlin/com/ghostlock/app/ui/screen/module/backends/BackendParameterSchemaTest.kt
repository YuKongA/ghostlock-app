package com.ghostlock.app.ui.screen.module.backends

import com.ghostlock.app.backend.declarations.BackendCve202643284
import com.ghostlock.app.backend.declarations.BackendCve202643499
import com.ghostlock.app.backend.declarations.BackendCve202698260
import com.ghostlock.app.ui.backend.BackendParameterScope
import com.ghostlock.app.ui.backend.BackendParameterSource
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * BackendSpec 参数/偏移声明守卫：只声明元数据、无重复、标注待提升项。
 */
class BackendParameterSchemaTest {
    @Test
    fun `98260DeclaresProfileAndCommonAbiParameters`() {
        val schema = BackendCve202698260.parameterSchema
        assertTrue(schema.parameters.size >= 30)
        assertEquals(
            schema.parameters.size,
            schema.parameters.map { it.name }.toSet().size,
        )
        assertTrue(schema.parameter("off_task_cred") != null)
        assertEquals(
            BackendParameterScope.COMMON_KERNEL_ABI,
            schema.parameter("off_task_cred")?.scope,
        )
        assertEquals(
            BackendParameterSource.PROFILE,
            schema.parameter("off_task_cred")?.source,
        )
    }

    @Test
    fun hardcodedFakeCredLayoutIsDeclaredAsPending() {
        val schema = BackendCve202698260.parameterSchema
        for (name in listOf(
            "fake_cred_usage_off",
            "fake_cred_caps_off",
            "fake_cred_security_off",
            "fake_cred_user_off",
            "fake_cred_user_ns_off",
            "fake_cred_ucounts_off",
            "fake_cred_group_info_off",
        )) {
            val decl = schema.parameter(name)
            assertTrue("missing " + name, decl != null)
            assertEquals(BackendParameterSource.COMPILE_CONSTANT, decl?.source)
            assertEquals(false, decl?.required)
        }
    }

    @Test
    fun builtinsWithoutDeclarationsStayEmpty() {
        assertTrue(BackendCve202643499.parameterSchema.parameters.isEmpty())
        assertTrue(BackendCve202643284.parameterSchema.parameters.isEmpty())
    }
}
