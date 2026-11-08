package com.ghostlock.app.ui.viewmodel

import com.ghostlock.app.domain.payload.PayloadAcceptance
import com.ghostlock.app.domain.payload.PayloadDefinition
import com.ghostlock.app.domain.payload.PayloadEditorPort
import com.ghostlock.app.domain.payload.PayloadKind
import com.ghostlock.app.domain.payload.PayloadRootManagerOption
import com.ghostlock.app.domain.payload.PayloadSlot
import com.ghostlock.app.domain.payload.ReadPayloadEditorUseCase
import com.ghostlock.app.domain.payload.SavePayloadEditorUseCase
import com.ghostlock.app.domain.run.PreviewRootScriptUseCase
import com.ghostlock.app.domain.run.RootScriptPreviewPort
import com.ghostlock.app.domain.finish.FinishDefinition
import java.util.ArrayDeque
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test

/** Locks preview to the exact save transaction even when a later edit is accepted. */
@OptIn(ExperimentalCoroutinesApi::class)
class PayloadViewModelPersistenceTest {
    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setMainDispatcher() {
        Dispatchers.setMain(dispatcher)
    }

    @After
    fun resetMainDispatcher() {
        Dispatchers.resetMain()
    }

    @Test
    fun previewUsesSecondEditWhileThirdEditIsForNextPreview() = runTest(dispatcher) {
        val slot = PayloadSlot(
            id = "payload-command",
            kind = PayloadKind.Command,
            acceptance = PayloadAcceptance.ExitZero,
            command = "one",
        )
        val port = FakePayloadEditorPort(PayloadDefinition(items = listOf(slot)))
        var previewPayload: PayloadDefinition? = null
        val viewModel = PayloadViewModel(
            readPayloadEditor = ReadPayloadEditorUseCase(port),
            savePayloadEditor = SavePayloadEditorUseCase(port),
            previewRootScript = PreviewRootScriptUseCase(
                object : RootScriptPreviewPort {
                    override fun preview(payload: PayloadDefinition, finish: FinishDefinition): String {
                        previewPayload = payload
                        return "/preview.sh"
                    }
                },
            ),
        )
        viewModel.openPayload()
        advanceUntilIdle()
        viewModel.updatePayload(slot.copy(command = "two"))
        var result: Result<String>? = null
        viewModel.savePreview { result = it }
        viewModel.updatePayload(slot.copy(command = "three"))
        advanceUntilIdle()
        assertEquals(3, port.pendingCount)
        port.drainAll()
        advanceUntilIdle()

        assertEquals("/preview.sh", requireNotNull(result).getOrThrow())
        assertEquals("two", requireNotNull(previewPayload).items.single().command)
        assertEquals("three", port.current.items.single().command)
    }

    private class FakePayloadEditorPort(initial: PayloadDefinition) : PayloadEditorPort {
        var current = initial
        private val pending = ArrayDeque<PendingTransform>()
        val pendingCount: Int
            get() = pending.size

        override fun submit(
            transform: (PayloadDefinition) -> PayloadDefinition,
        ): Deferred<PayloadDefinition> {
            val completion = CompletableDeferred<PayloadDefinition>()
            pending.addLast(PendingTransform(transform, completion))
            return completion
        }
        override fun snapshot(): Deferred<PayloadDefinition> = CompletableDeferred(current)
        override fun rootManagerOptions(): List<PayloadRootManagerOption> = emptyList()

        /** Executes every already-accepted transform in actor order. */
        fun drainAll() {
            while (pending.isNotEmpty()) {
                val operation = pending.removeFirst()
                current = operation.transform(current)
                operation.completion.complete(current)
            }
        }

        /** One queued transform and its exact transaction result handle. */
        private data class PendingTransform(
            val transform: (PayloadDefinition) -> PayloadDefinition,
            val completion: CompletableDeferred<PayloadDefinition>,
        )
    }
}
