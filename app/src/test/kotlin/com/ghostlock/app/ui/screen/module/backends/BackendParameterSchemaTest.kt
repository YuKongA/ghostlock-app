package com.ghostlock.app.ui.screen.module.backends

import com.ghostlock.app.backend.backends.cve43284.BackendCve43284
import com.ghostlock.app.backend.backends.cve43499.BackendCve43499
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * BackendSpec 参数/偏移声明守卫：只声明元数据、无重复、标注待提升项。
 */
class BackendParameterSchemaTest {


    @Test
    fun builtinsWithoutDeclarationsStayEmpty() {
        assertTrue(BackendCve43499.parameterSchema.parameters.isEmpty())
        assertTrue(BackendCve43284.parameterSchema.parameters.isEmpty())
    }
}
