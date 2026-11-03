package com.ghostlock.app.core.flow

import kotlin.reflect.KClass
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** B1：`FlowEntryRegistry` 正向/未知/重复/非法/不可变矩阵。 */
class FlowEntryRegistryTest {
    private class Request : FlowRequest

    private object RegisteredFlow : Flow<Request> {
        override val backendClass: KClass<out Flow<*>> = RegisteredFlow::class
        override suspend fun run(request: Request): FlowResult =
            FlowResult.Ok(emptyList(), emptyMap())
        override fun stop() = Unit

        @FlowEntry
        fun entry(env: FlowHostEnv): FlowResult = FlowResult.Ok(emptyList(), emptyMap())
    }

    private class NotFlow {
        @FlowEntry
        fun entry(env: FlowHostEnv): FlowResult = FlowResult.Ok(emptyList(), emptyMap())
    }

    private val symbol = FlowSymbol(RegisteredFlow::class.java.name, "entry")

    @Test
    fun `registry resolves registered symbol`() {
        val registry = FlowEntryRegistry.of(listOf(symbol))
        assertEquals(setOf(symbol), registry.symbols)
        val resolution = registry.resolve(symbol)
        assertTrue(resolution is FlowEntryResolution.Resolved)
        assertEquals(symbol, (resolution as FlowEntryResolution.Resolved).entry.symbol)
    }

    @Test
    fun `unregistered symbol is rejected`() {
        val registry = FlowEntryRegistry.of(listOf(symbol))
        val other = FlowSymbol("com.ghostlock.app.core.flow.OtherFlow", "entry")
        val resolution = registry.resolve(other)
        assertEquals(
            "flow-symbol:unregistered",
            (resolution as FlowEntryResolution.Rejected).reason,
        )
    }

    @Test
    fun `duplicate symbol fails closed`() {
        val error = runCatching {
            FlowEntryRegistry.of(listOf(symbol, symbol))
        }.exceptionOrNull()
        assertTrue(error is IllegalStateException)
        assertTrue(
            error!!.message!!.startsWith("flow-registry:duplicate-symbol:" + symbol.canonical),
        )
    }

    @Test
    fun `invalid candidate fails closed`() {
        val invalid = FlowSymbol(NotFlow::class.java.name, "entry")
        val error = runCatching {
            FlowEntryRegistry.of(listOf(invalid))
        }.exceptionOrNull()
        assertTrue(error is IllegalStateException)
        assertTrue(error!!.message!!.contains("flow-registry:invalid:flow-symbol:not-a-flow:"))
    }

    @Test
    fun `symbols view is stable and read only`() {
        val registry = FlowEntryRegistry.of(listOf(symbol))
        val first = registry.symbols
        val second = registry.symbols
        assertEquals(first, second)
        assertEquals(1, first.size)
        assertTrue(symbol in first)
    }
}
