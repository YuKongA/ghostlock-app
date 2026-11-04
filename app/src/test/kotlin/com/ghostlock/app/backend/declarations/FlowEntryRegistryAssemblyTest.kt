package com.ghostlock.app.backend.declarations

import com.ghostlock.app.BuildConfig
import com.ghostlock.app.backend.diagnostics.DiagnosticFlowEntries
import com.ghostlock.app.backend.diagnostics.DiagnosticHandoffFlow
import com.ghostlock.app.backend.diagnostics.DiagnosticOkFlow
import com.ghostlock.app.backend.diagnostics.DiagnosticKeepaliveFlow
import com.ghostlock.app.backend.diagnostics.DiagnosticSlowFlow
import com.ghostlock.app.backend.backends.cve43284glued.BackendCve43284Glued
import com.ghostlock.app.backend.backends.debugprobe.BackendDebugProbe
import com.ghostlock.app.backend.backends.cve43499glued.BackendCve43499Glued
import com.ghostlock.app.backend.registry.Backends
import com.ghostlock.app.core.flow.FlowEntryRegistry
import com.ghostlock.app.core.flow.FlowEntryResolution
import com.ghostlock.app.core.flow.FlowSymbol
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** G9：BackendSpec→registry 组装可信性（纯 JVM；无设备/Robolectric）。 */
class FlowEntryRegistryAssemblyTest {
    /** @return Immutable registry membership for one assertion transaction. */
    private fun snapshot(): List<BackendSpec> = Backends.all.toList()

    @Test
    fun `production symbols come only from spec flowSymbols`() {
        val expected = buildList {
            add(requireNotNull(BackendCve43499Glued.flowSymbol) { "43499-Glued must be migrated" })
            add(requireNotNull(BackendCve43284Glued.flowSymbol) { "43284Glued must be migrated" })
            if (BuildConfig.DEBUG) {
                add(requireNotNull(BackendDebugProbe.flowSymbol) { "debug probe must expose flowSymbol in debug" })
            }
        }
        assertEquals(expected, FlowEntryRegistryAssembly.productionSymbols(snapshot()))
        assertEquals(
            "debug probe must only be registered in debug builds",
            BuildConfig.DEBUG,
            FlowEntryRegistryAssembly.productionSymbols(snapshot()).contains(BackendDebugProbe.flowSymbol),
        )
        assertTrue(
            "diagnostic fixtures must not be production symbols",
            FlowEntryRegistryAssembly.productionSymbols(snapshot()).none { symbol ->
                symbol == diagnosticSymbols().first()
            },
        )
    }

    @Test
    fun `production registry resolves migrated specs and rejects diagnostics`() {
        val registry = FlowEntryRegistryAssembly.productionRegistry(snapshot())
        val glued = buildList {
            add(requireNotNull(BackendCve43499Glued.flowSymbol))
            add(requireNotNull(BackendCve43284Glued.flowSymbol))
            if (BuildConfig.DEBUG) add(requireNotNull(BackendDebugProbe.flowSymbol))
        }
        for (symbol in glued) {
            val resolved = registry.resolve(symbol)
            assertTrue("expected Resolved for $symbol", resolved is FlowEntryResolution.Resolved)
            assertEquals(
                symbol,
                (resolved as FlowEntryResolution.Resolved).entry.symbol,
            )
        }
        val diagnostic = registry.resolve(diagnosticSymbols().first())
        assertEquals(
            "flow-symbol:unregistered",
            (diagnostic as FlowEntryResolution.Rejected).reason,
        )
    }

    @Test
    fun `host registry follows debug flag`() {
        val production = FlowEntryRegistryAssembly.productionSymbols(snapshot()).toSet()
        val diagnostics = diagnosticSymbols().toSet()
        val host = FlowEntryRegistryAssembly.hostRegistry(snapshot())
        if (BuildConfig.DEBUG) {
            assertEquals(production + diagnostics, host.symbols)
            for (symbol in diagnostics) {
                assertTrue(
                    "debug host must resolve $symbol",
                    host.resolve(symbol) is FlowEntryResolution.Resolved,
                )
            }
        } else {
            assertEquals(production, host.symbols)
            for (symbol in diagnostics) {
                assertEquals(
                    "flow-symbol:unregistered",
                    (host.resolve(symbol) as FlowEntryResolution.Rejected).reason,
                )
            }
        }
    }

    @Test
    fun `every selectable spec is registered bidirectionally`() {
        val symbols = FlowEntryRegistryAssembly.productionSymbols(snapshot())
        for (spec in Backends.all.filter { it.selectable }) {
            val symbol = spec.flowSymbol
            assertTrue("selectable spec without flowSymbol: " + spec.key, symbol != null)
            assertTrue("symbol missing from production: $symbol", symbol in symbols)
        }
        val specBySymbol = Backends.all.mapNotNull { spec ->
            spec.flowSymbol?.let { symbol -> symbol to spec }
        }.toMap()
        for (symbol in symbols) {
            assertTrue("symbol without spec: $symbol", specBySymbol.containsKey(symbol))
        }
    }

    @Test
    fun `registry identity is deterministic and order sensitive`() {
        val entries = snapshot()
        val first = BackendRegistryIdentityFactory.capture(entries)
        val repeated = BackendRegistryIdentityFactory.capture(entries.toList())
        val reordered = BackendRegistryIdentityFactory.capture(entries.reversed())

        assertEquals(first, repeated)
        assertNotEquals(first, reordered)
        assertTrue(first.contentHash.matches(Regex("[0-9a-f]{64}")))
    }

    @Test
    fun `unknown and duplicate symbols stay fail closed`() {
        val glued = requireNotNull(BackendCve43499Glued.flowSymbol)
        val duplicate = runCatching {
            FlowEntryRegistry.of(listOf(glued, glued))
        }.exceptionOrNull()
        assertTrue(duplicate is IllegalStateException)
        assertTrue(
            duplicate!!.message!!.startsWith("flow-registry:duplicate-symbol:"),
        )

        val unknown = runCatching {
            FlowEntryRegistry.of(
                listOf(FlowSymbol("com.ghostlock.app.core.flow.NoSuchFlowFixture", "entry")),
            )
        }.exceptionOrNull()
        assertTrue(unknown is IllegalStateException)
        assertTrue(
            unknown!!.message!!.startsWith("flow-registry:invalid:flow-symbol:class-not-found:"),
        )
    }

    /** @return 诊断 fixture 符号集合（仅 debug host allowlist）。 */
    private fun diagnosticSymbols(): List<FlowSymbol> = listOf(
        FlowSymbol(DiagnosticOkFlow::class.java.name, "entry"),
        FlowSymbol(DiagnosticHandoffFlow::class.java.name, "entry"),
        FlowSymbol(DiagnosticSlowFlow::class.java.name, "entry"),
        FlowSymbol(DiagnosticKeepaliveFlow::class.java.name, "entry"),
    ).also { expected ->
        assertEquals(expected, DiagnosticFlowEntries.symbols)
    }
}
