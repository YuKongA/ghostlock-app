package com.ghostlock.app.domain.run

import com.ghostlock.app.domain.finish.FinishDefinition
import com.ghostlock.app.domain.payload.PayloadDefinition
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Proves preview consumes the exact supplied Payload and never needs a Finish repository. */
class PreviewRootScriptUseCaseTest {
    @Test
    fun previewUsesExactPayloadAndExplicitEmptyFinish() {
        val expected = PayloadDefinition(safeMode = true)
        var receivedPayload: PayloadDefinition? = null
        var receivedFinish: FinishDefinition? = null
        val useCase = PreviewRootScriptUseCase(
            previewPort = object : RootScriptPreviewPort {
                override fun preview(payload: PayloadDefinition, finish: FinishDefinition): String {
                    receivedPayload = payload
                    receivedFinish = finish
                    return "/preview/payload.sh"
                }
            },
        )
        assertEquals("/preview/payload.sh", useCase(expected))
        assertEquals(expected, receivedPayload)
        assertTrue(requireNotNull(receivedFinish).items.isEmpty())
    }
}
