package com.ghostlock.app.core.run

import com.ghostlock.app.data.run.RootManagerArtifactResolver
import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.domain.backend.BackendSelectionPolicy
import com.ghostlock.app.domain.backend.ExecutionDomain
import com.ghostlock.app.domain.finish.FinishDefinition
import com.ghostlock.app.domain.finish.FinishRepository
import com.ghostlock.app.domain.payload.PayloadDefinition
import com.ghostlock.app.domain.payload.PayloadRepository
import com.ghostlock.app.domain.run.RunCapability
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment

/** Verifies run cutoffs return actor-position values rather than later live values. */
@RunWith(RobolectricTestRunner::class)
class RunInputFreezerPersistenceTest {
    @Test
    fun cutoffExcludesLaterAcceptedEdit() = runTest {
        val payload = FakePayloadRepository(PayloadDefinition(safeMode = false))
        val finish = FakeFinishRepository(FinishDefinition())
        val context = RuntimeEnvironment.getApplication()
        val freezer = RunInputFreezer(
            context = context,
            payloadRepository = payload,
            finishRepository = finish,
            rootManagerResolver = RootManagerArtifactResolver(context),
        )
        payload.current = PayloadDefinition(safeMode = true)
        val cutoff = freezer.cutoff(productSpec())
        payload.current = PayloadDefinition(safeMode = false)

        assertEquals(true, cutoff.payload.await().safeMode)
        assertEquals(false, payload.snapshot().await().safeMode)
    }

    private fun productSpec(): BackendDescriptor = BackendDescriptor(
        key = "product-test",
        selectionPolicy = BackendSelectionPolicy.SELECTABLE,
        abortPolicy = AbortPolicy.SAFE_DURING_RUN,
        executionDomain = ExecutionDomain.APP,
        runCapability = RunCapability.PRODUCT_ROOT,
    )

    private class FakePayloadRepository(initial: PayloadDefinition) : PayloadRepository {
        var current = initial
        override fun submit(
            transform: (PayloadDefinition) -> PayloadDefinition,
        ): Deferred<PayloadDefinition> {
            current = transform(current)
            return CompletableDeferred(current)
        }
        override fun snapshot(): Deferred<PayloadDefinition> = CompletableDeferred(current)
    }

    private class FakeFinishRepository(initial: FinishDefinition) : FinishRepository {
        var current = initial
        override fun submit(
            transform: (FinishDefinition) -> FinishDefinition,
        ): Deferred<FinishDefinition> {
            current = transform(current)
            return CompletableDeferred(current)
        }
        override fun snapshot(): Deferred<FinishDefinition> = CompletableDeferred(current)
    }
}
