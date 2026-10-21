package com.ghostlock.app.engine.kernelmemory

import com.ghostlock.app.core.generated.Discover43499
import com.ghostlock.app.core.generated.Prepare43499
import com.ghostlock.app.core.steps.StepId

/** 记录调用顺序、可逐阶段注入结果的 fake port。 */
internal class FakeBackend43499Port : Backend43499Port {
    override var capabilities: NativeCapabilities = NativeCapabilities(true, true, true)

    val calls = mutableListOf<String>()
    val writes = mutableListOf<Write>()

    var createResult: PortResult<Unit> = PortResult.Ok(Unit)
    var initializeResult: PortResult<Unit> = PortResult.Ok(Unit)
    var prepareResult: PortResult<Facts43499> = PortResult.Ok(TestDtos.facts())
    var discoverResult: PortResult<Facts43499> = PortResult.Ok(TestDtos.facts())
    var spawnResult: PortResult<Facts43499> = PortResult.Ok(TestDtos.facts())
    var probeHandler: () -> PortResult<Long> = { PortResult.Ok(0L) }
    var finishResult: PortResult<Facts43499> = PortResult.Ok(TestDtos.facts())
    var writeHandler: (Write) -> PortResult<Unit> = { PortResult.Ok(Unit) }
    var closeResult: PortResult<Unit> = PortResult.Ok(Unit)

    override fun create(): PortResult<Unit> {
        calls += "create"
        return createResult
    }

    override fun initialize(session: KernelSession): PortResult<Unit> {
        calls += "initialize"
        return initializeResult
    }

    override fun prepare(request: Prepare43499): PortResult<Facts43499> {
        calls += "prepare"
        return prepareResult
    }

    override fun discover(request: Discover43499): PortResult<Facts43499> {
        calls += "discover:" + request.prepareGeneration
        return discoverResult
    }

    override fun spawnVictim(): PortResult<Facts43499> {
        calls += "spawn"
        return spawnResult
    }

    override fun victimProbe(kind: Int): PortResult<Long> {
        calls += "probe:" + kind
        return probeHandler()
    }

    override fun finishVictim(action: Int, victimGeneration: Long): PortResult<Facts43499> {
        calls += "finish:" + action + ":" + victimGeneration
        return finishResult
    }

    override fun writeStep(
        step: KernelStep,
        address: Long,
        value: Long,
        form: Int,
        offset: Long,
    ): PortResult<Unit> {
        val write = Write(step.step, address, value, form, offset)
        writes += write
        calls += "write:" + step.step.token
        return writeHandler(write)
    }

    override fun close(): PortResult<Unit> {
        calls += "close"
        return closeResult
    }

    data class Write(
        val step: StepId,
        val address: Long,
        val value: Long,
        val form: Int,
        val offset: Long,
    )
}
