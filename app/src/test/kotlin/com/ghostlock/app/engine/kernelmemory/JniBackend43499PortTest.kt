package com.ghostlock.app.engine.kernelmemory

import com.ghostlock.app.core.generated.CredentialLayout43499
import com.ghostlock.app.core.generated.Discover43499
import com.ghostlock.app.core.generated.FinishVictim43499
import com.ghostlock.app.core.generated.Init43499
import com.ghostlock.app.core.generated.KernelLayout43499
import com.ghostlock.app.core.generated.Prepare43499
import com.ghostlock.app.core.generated.TargetOffsets43499
import com.ghostlock.app.core.steps.StepId
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class JniBackend43499PortTest {
    @Test
    fun generationsAreDetectedFromSnapshotFieldNames() {
        val old = FakeNative43499Api(fields = baseFields())
        val oldPort = JniBackend43499Port(old)
        assertFalse(oldPort.capabilities.generations)
        assertTrue(oldPort.capabilities.formPropagation.not())
        assertTrue(oldPort.capabilities.perStepTargetValue.not())

        val fresh = FakeNative43499Api(fields = generationFields())
        val freshPort = JniBackend43499Port(fresh)
        assertTrue(freshPort.capabilities.generations)
    }

    @Test
    fun discoverWithoutGenerationsDoesNotTouchNative() {
        val native = FakeNative43499Api(fields = baseFields())
        val port = JniBackend43499Port(native)
        port.create()

        val answer = port.discover(TestDtos.discover(1L))

        assertTrue(answer is PortResult.Refused)
        assertEquals(PortCode.UNSUPPORTED, (answer as PortResult.Refused).refusal.code)
        assertEquals("generation-not-published", answer.refusal.detail)
        assertFalse(native.calls.contains("discover"))
    }

    @Test
    fun prepareMapsCallCodeAndNamedRefusal() {
        val native = FakeNative43499Api(fields = baseFields()).apply {
            prepareCode = -1
            prepareRefusal = "not-ready:prepare"
        }
        val port = JniBackend43499Port(native)
        port.create()

        val answer = port.prepare(TestDtos.prepare())

        assertTrue(answer is PortResult.Refused)
        val refused = answer as PortResult.Refused
        assertEquals(PortCode.NOT_READY, refused.refusal.code)
        assertEquals("not-ready:prepare", refused.refusal.detail)
    }

    @Test
    fun prepareDecodesSnapshotByFieldName() {
        val fields = generationFields()
        val native = FakeNative43499Api(fields = fields).apply {
            prepareValues = snapshotValues(fields, prepared = true, pageBase = 0x1234L)
        }
        val port = JniBackend43499Port(native)
        port.create()

        val answer = port.prepare(TestDtos.prepare())

        assertTrue(answer is PortResult.Ok)
        val facts = (answer as PortResult.Ok).value
        assertTrue(facts.prepared)
        assertEquals(0x1234L, facts.pageBase)
        assertTrue(facts.generationsAvailable)
        assertEquals(1L, facts.prepareGeneration)
    }

    @Test
    fun snapshotWidthOtherThanEightIsRefused() {
        val fields = generationFields()
        val values = snapshotValues(fields)
        values[fields.indexOf("width")] = 16L
        val native = FakeNative43499Api(fields = fields).apply { prepareValues = values }
        val port = JniBackend43499Port(native)
        port.create()

        val answer = port.prepare(TestDtos.prepare())

        assertTrue(answer is PortResult.Refused)
        val refused = answer as PortResult.Refused
        assertEquals(PortCode.BAD_PARAMETERS, refused.refusal.code)
        assertEquals("snapshot-width:16", refused.refusal.detail)
    }

    @Test
    fun missingRequiredSnapshotFieldIsNamed() {
        val broken = baseFields().filter { it != "page_base" }.toTypedArray()
        val native = FakeNative43499Api(fields = broken)
        val port = JniBackend43499Port(native)
        port.create()

        val answer = port.prepare(TestDtos.prepare())

        assertTrue(answer is PortResult.Refused)
        val refused = answer as PortResult.Refused
        assertEquals(PortCode.FAILED, refused.refusal.code)
        assertTrue(refused.refusal.detail.startsWith("snapshot-decode:"))
    }

    @Test
    fun unknownCallCodeFoldsToFailed() {
        val native = FakeNative43499Api(fields = generationFields()).apply { prepareCode = -99 }
        val port = JniBackend43499Port(native)
        port.create()

        val answer = port.prepare(TestDtos.prepare())

        assertTrue(answer is PortResult.Refused)
        assertEquals(PortCode.FAILED, (answer as PortResult.Refused).refusal.code)
    }

    @Test
    fun closeRunsShutdownThenDestroyAndIsIdempotent() {
        val native = FakeNative43499Api(fields = baseFields())
        val port = JniBackend43499Port(native)
        port.create()

        assertTrue(port.close() is PortResult.Ok)
        assertEquals(listOf("shutdown", "destroy"), native.calls.takeLast(2))
        val count = native.calls.size
        assertTrue(port.close() is PortResult.Ok)
        assertEquals(count, native.calls.size)
    }

    @Test
    fun libraryMissingIsNamed() {
        val native = FakeNative43499Api(fields = baseFields()).apply { loaded = false }
        val port = JniBackend43499Port(native)

        val answer = port.create()

        assertTrue(answer is PortResult.Refused)
        assertEquals("library-missing", (answer as PortResult.Refused).refusal.detail)
    }

    @Test
    fun writeStepRefusesFormBeforeAnyNativeWrite() {
        val native = FakeNative43499Api(fields = generationFields())
        val port = JniBackend43499Port(native)
        port.create()
        val step = KernelStep(StepId.W1, route = 1, offset = 0L, address = 0x1000L, target = 0x1000L, value = 0L)

        val answer = port.writeStep(step, 0x1000L, 0L, 1, 0L)

        assertTrue(answer is PortResult.Refused)
        assertEquals("form-not-propagated", (answer as PortResult.Refused).refusal.detail)
        assertFalse(native.calls.any { it.startsWith("write") })
    }

    private fun baseFields(): Array<String> = arrayOf(
        "phase", "flags", "width", "page_base", "word0", "word1", "payload_page",
        "payload_base", "alias_delta", "found", "mm_struct", "victim_task", "victim_pid",
    )

    private fun generationFields(): Array<String> = baseFields() + arrayOf(
        "prepare_generation", "discovery_generation", "victim_generation",
    )

    private fun snapshotValues(
        fields: Array<String>,
        prepared: Boolean = true,
        pageBase: Long = 0x1000L,
        found: Long = 0x2000L,
        victimTask: Long = 0x3000L,
    ): LongArray {
        val out = LongArray(fields.size)
        val at = fields.withIndex().associate { (index, name) -> name to index }
        out[at.getValue("flags")] = (if (prepared) Facts43499.PREPARED_BIT else 0L) or
            Facts43499.DISCOVERED_BIT or Facts43499.VICTIM_BIT
        out[at.getValue("width")] = 8L
        out[at.getValue("page_base")] = pageBase
        out[at.getValue("found")] = found
        out[at.getValue("victim_task")] = victimTask
        at["prepare_generation"]?.let { out[it] = 1L }
        at["discovery_generation"]?.let { out[it] = 1L }
        at["victim_generation"]?.let { out[it] = 1L }
        return out
    }
}

/** 可脚本化的 native 函数面；不加载 .so。 */
internal class FakeNative43499Api(
    var fields: Array<String> = arrayOf(
        "phase", "flags", "width", "page_base", "word0", "word1", "payload_page",
        "payload_base", "alias_delta", "found", "mm_struct", "victim_task", "victim_pid",
    ),
    var loaded: Boolean = true,
    var created: Long = 7L,
    var prepareCode: Int = 0,
    var prepareRefusal: String = "",
    var prepareValues: LongArray? = null,
    var discoverValues: LongArray? = null,
    var spawnValues: LongArray? = null,
    var finishValues: LongArray? = null,
    var probeValue: Long = 0L,
    var shutdownCode: Int = 0,
    var destroyCode: Int = 0,
) : Native43499Api {
    val calls = mutableListOf<String>()

    override fun ensureLoaded(): Boolean {
        calls += "ensureLoaded"
        return loaded
    }

    override fun create(): Long {
        calls += "create"
        return created
    }

    override fun initialize(
        handle: Long,
        init: Init43499,
        layout: KernelLayout43499,
        credential: CredentialLayout43499,
        targets: TargetOffsets43499,
        refusalOut: Array<String>,
    ): Int {
        calls += "initialize"
        return 0
    }

    override fun snapshot(handle: Long, out: LongArray, refusalOut: Array<String>): Int {
        calls += "snapshot"
        return fill(out, prepareValues)
    }

    override fun prepare(handle: Long, request: Prepare43499, out: LongArray, refusalOut: Array<String>): Int {
        calls += "prepare"
        if (prepareCode != 0) refusalOut[0] = prepareRefusal
        return if (prepareCode == 0) fill(out, prepareValues) else prepareCode
    }

    override fun discover(handle: Long, request: Discover43499, out: LongArray, refusalOut: Array<String>): Int {
        calls += "discover"
        return fill(out, discoverValues)
    }

    override fun spawnVictim(handle: Long, out: LongArray, refusalOut: Array<String>): Int {
        calls += "spawn"
        return fill(out, spawnValues)
    }

    override fun finishVictim(
        handle: Long,
        request: FinishVictim43499,
        out: LongArray,
        refusalOut: Array<String>,
    ): Int {
        calls += "finish"
        return fill(out, finishValues)
    }

    override fun victimProbe(handle: Long, kind: Int, out: LongArray, refusalOut: Array<String>): Int {
        calls += "probe"
        out[0] = probeValue
        return 0
    }

    override fun shutdown(handle: Long, refusalOut: Array<String>): Int {
        calls += "shutdown"
        return shutdownCode
    }

    override fun destroy(handle: Long, refusalOut: Array<String>): Int {
        calls += "destroy"
        return destroyCode
    }

    override fun snapshotFields(): Array<String> {
        calls += "snapshotFields"
        return fields
    }

    private fun fill(out: LongArray, values: LongArray?): Int {
        if (values == null) return 0
        require(values.size == out.size) { "values/out size mismatch" }
        values.copyInto(out)
        return 0
    }
}
