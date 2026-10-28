package com.ghostlock.app.ui.backend

import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 执行体多态守卫：App/Shizuku 同一 symbol 调用结构；App 在专用 JVM 线程解析执行；
 * Shizuku 不得静默回退 App。
 */
class FlowLaunchParentTest {
    private class FakeResolver : FlowSymbolResolver {
        var symbol: String? = null
        var threadName: String = ""
        override suspend fun resolve(
            context: android.content.Context?,
            invocation: FlowSymbolInvocation,
            onLine: (String) -> Unit,
        ): BackendRunUiState {
            symbol = invocation.symbol
            threadName = Thread.currentThread().name
            return BackendRunUiState.Refused(listOf("ran:" + invocation.symbol))
        }
    }

    @Test
    fun appExecutorUsesSymbolOnDedicatedJvmThread() = runBlocking {
        val resolver = FakeResolver()
        val result = LocalThreadFlowExecutor(resolver).launch(
            context = null,
            invocation = FlowSymbolInvocation("CVE-2026-98260"),
            onLine = {},
        )
        assertTrue(result is BackendRunUiState.Refused)
        assertEquals("CVE-2026-98260", resolver.symbol)
        assertTrue(resolver.threadName.startsWith("ghostlock-attack"))
    }

    @Test
    fun shizukuExecutorUsesSameSymbolContractWithoutLocalLambda() = runBlocking {
        val result = ShizukuHostFlowExecutor().launch(
            context = null,
            invocation = FlowSymbolInvocation("CVE-2026-98260"),
            onLine = {},
        )
        assertTrue(result is BackendRunUiState.Refused)
        assertEquals(
            "flow-launcher:shizuku-remote-pending:CVE-2026-98260",
            (result as BackendRunUiState.Refused).reasons.single(),
        )
    }

    @Test
    fun parentSelectsExecutorPolymorphically() {
        assertTrue(
            FlowLaunchParent.executorFor(AttackExecutorState(AttackExecutorKind.APP, true))
                is LocalThreadFlowExecutor,
        )
        assertTrue(
            FlowLaunchParent.executorFor(AttackExecutorState(AttackExecutorKind.SHIZUKU, true))
                is ShizukuHostFlowExecutor,
        )
    }

    @Test
    fun blankSymbolIsRejected() {
        var thrown: Throwable? = null
        try {
            FlowSymbolInvocation("")
        } catch (problem: Throwable) {
            thrown = problem
        }
        assertTrue(thrown is IllegalArgumentException)
    }
}
