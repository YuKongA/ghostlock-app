package com.ghostlock.app.core.flow

import java.lang.annotation.Retention
import java.lang.annotation.RetentionPolicy
import java.lang.annotation.Target
import kotlin.reflect.KClass
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** B1：FlowEntry 反射校验正/负例矩阵（纯 JVM，只用 java.lang.reflect）。 */
class FlowEntryValidationTest {
    @Test
    fun `annotation contract is function-targeted and runtime retained`() {
        val target = FlowEntry::class.java.getAnnotation(Target::class.java)
        val retention = FlowEntry::class.java.getAnnotation(Retention::class.java)
        assertEquals(1, target?.value?.size)
        assertEquals(java.lang.annotation.ElementType.METHOD, target?.value?.first())
        assertEquals(RetentionPolicy.RUNTIME, retention?.value)
    }

    @Test
    fun `resolves valid object and invokes entry`() {
        val resolution = FlowEntryValidator.validate(FlowSymbol(ValidFlow::class.java.name, "entry"))
        assertTrue(resolution is FlowEntryResolution.Resolved)
        val env = fixtureEnv()
        assertTrue((resolution as FlowEntryResolution.Resolved).entry.invoke(env) is FlowResult.Ok)
    }

    @Test
    fun `rejects class that does not implement Flow`() {
        assertEquals("flow-symbol:not-a-flow", reasonOf(NotAFlow::class.java))
    }

    @Test
    fun `rejects missing annotation`() {
        assertEquals("flow-symbol:not-annotated", reasonOf(NotAnnotatedFlow::class.java))
    }

    @Test
    fun `rejects private method`() {
        assertEquals("flow-symbol:method-not-public", reasonOf(PrivateMethodFlow::class.java))
    }

    @Test
    fun `rejects jvm static entry`() {
        assertEquals("flow-symbol:method-static", reasonOf(StaticFlow::class.java))
    }

    @Test
    fun `rejects companion only entry`() {
        assertEquals("flow-symbol:method-not-found", reasonOf(CompanionOnlyFlow::class.java))
    }

    @Test
    fun `rejects inherited entry on subclass`() {
        assertEquals(
            "flow-symbol:declaring-class-mismatch",
            reasonOf(InheritedFlow::class.java),
        )
    }

    @Test
    fun `rejects overloaded method names`() {
        assertEquals("flow-symbol:overloaded", reasonOf(OverloadedFlow::class.java))
    }

    @Test
    fun `rejects suspend entry`() {
        assertEquals("flow-symbol:wrong-signature", reasonOf(SuspendFlow::class.java))
    }

    @Test
    fun `rejects default parameter entry`() {
        assertEquals("flow-symbol:wrong-signature", reasonOf(DefaultParamFlow::class.java))
    }

    @Test
    fun `rejects covariant return subtype`() {
        assertEquals("flow-symbol:wrong-return", reasonOf(CovariantReturnFlow::class.java))
    }

    @Test
    fun `rejects inner class via not instantiable`() {
        assertEquals(
            "flow-symbol:not-instantiable",
            reasonOf(InnerHost.InnerFlow::class.java),
        )
    }

    @Test
    fun `rejects generic method`() {
        assertEquals("flow-symbol:wrong-signature", reasonOf(GenericFlow::class.java))
    }

    @Test
    fun `rejects varargs entry`() {
        assertEquals("flow-symbol:method-varargs", reasonOf(VarargsFlow::class.java))
    }

    @Test
    fun `rejects private constructor`() {
        assertEquals("flow-symbol:not-instantiable", reasonOf(PrivateCtorFlow::class.java))
    }

    @Test
    fun `rejects unknown class`() {
        val resolution = FlowEntryValidator.validate(
            FlowSymbol("com.ghostlock.app.core.flow.NoSuchFlowFixture", "entry"),
        )
        assertEquals(
            "flow-symbol:class-not-found",
            (resolution as FlowEntryResolution.Rejected).reason,
        )
    }

    @Test
    fun `bridge method presence is rejected via overloaded equivalent negative`() {
        // JVM 为协变 override 生成 bridge；校验顺序（同名多方法 → overloaded 先于
        // bridge）使 bridge 单独不可触达，因此用等价可构造负例（overloaded）。
        val hasBridge = BridgeFlow::class.java.declaredMethods.any { it.isBridge }
        assertTrue("fixture must actually contain a bridge method", hasBridge)
        assertEquals("flow-symbol:overloaded", reasonOf(BridgeFlow::class.java))
    }

    private fun reasonOf(clazz: Class<*>, method: String = "entry"): String {
        val resolution = FlowEntryValidator.validate(FlowSymbol(clazz.name, method))
        assertTrue("expected rejection for ${clazz.name}", resolution is FlowEntryResolution.Rejected)
        return (resolution as FlowEntryResolution.Rejected).reason
    }

    private fun fixtureEnv(): FlowHostEnv = object : FlowHostEnv {
        override val params: FlowHostParams = FlowHostParams.of(emptyMap())
        override val payload: ByteArray? = null
        override fun log(line: String) = Unit
        override fun isCancelled(): Boolean = false
    }
}

// ---- fixtures（顶层 private；避免嵌套类使用 outer 实例） ----

private class FixtureRequest : FlowRequest

private fun fixtureOk(): FlowResult = FlowResult.Ok(emptyList(), emptyMap())

private open class BaseFlow : Flow<FixtureRequest> {
    override val backendClass: KClass<out Flow<*>> = BaseFlow::class
    override suspend fun run(request: FixtureRequest): FlowResult = fixtureOk()
    override fun stop() = Unit
}

private object ValidFlow : BaseFlow() {
    @FlowEntry
    fun entry(env: FlowHostEnv): FlowResult = fixtureOk()
}

private class NotAFlow {
    @FlowEntry
    fun entry(env: FlowHostEnv): FlowResult = fixtureOk()
}

private class NotAnnotatedFlow : BaseFlow() {
    fun entry(env: FlowHostEnv): FlowResult = fixtureOk()
}

private class PrivateMethodFlow : BaseFlow() {
    @FlowEntry
    private fun entry(env: FlowHostEnv): FlowResult = fixtureOk()
}

private class StaticFlow : BaseFlow() {
    companion object {
        @JvmStatic
        @FlowEntry
        fun entry(env: FlowHostEnv): FlowResult = fixtureOk()
    }
}

private class CompanionOnlyFlow : BaseFlow() {
    companion object {
        @FlowEntry
        fun entry(env: FlowHostEnv): FlowResult = fixtureOk()
    }
}

private open class InheritedBaseFlow : BaseFlow() {
    @FlowEntry
    open fun entry(env: FlowHostEnv): FlowResult = fixtureOk()
}

private class InheritedFlow : InheritedBaseFlow()

private class OverloadedFlow : BaseFlow() {
    @FlowEntry
    fun entry(env: FlowHostEnv): FlowResult = fixtureOk()

    @FlowEntry
    fun entry(env: FlowHostEnv, extra: Int): FlowResult = fixtureOk()
}

private class SuspendFlow : BaseFlow() {
    @FlowEntry
    suspend fun entry(env: FlowHostEnv): FlowResult = fixtureOk()
}

private class DefaultParamFlow : BaseFlow() {
    @FlowEntry
    fun entry(env: FlowHostEnv, depth: Int = 0): FlowResult = fixtureOk()
}

private class CovariantReturnFlow : BaseFlow() {
    @FlowEntry
    fun entry(env: FlowHostEnv): FlowResult.Ok = FlowResult.Ok(emptyList(), emptyMap())
}

private class InnerHost {
    inner class InnerFlow : BaseFlow() {
        @FlowEntry
        fun entry(env: FlowHostEnv): FlowResult = fixtureOk()
    }
}

private class GenericFlow : BaseFlow() {
    @FlowEntry
    fun <T> entry(env: FlowHostEnv): FlowResult = fixtureOk()
}

private class VarargsFlow : BaseFlow() {
    @FlowEntry
    fun entry(vararg env: FlowHostEnv): FlowResult = fixtureOk()
}

private class PrivateCtorFlow private constructor() : BaseFlow() {
    @FlowEntry
    fun entry(env: FlowHostEnv): FlowResult = fixtureOk()
}

private interface BridgeContract {
    fun entry(env: FlowHostEnv): FlowResult
}

private class BridgeFlow : BaseFlow(), BridgeContract {
    override fun entry(env: FlowHostEnv): FlowResult.Ok = FlowResult.Ok(emptyList(), emptyMap())
}
