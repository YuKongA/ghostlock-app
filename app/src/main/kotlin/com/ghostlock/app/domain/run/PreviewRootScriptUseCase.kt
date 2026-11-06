package com.ghostlock.app.domain.run

import com.ghostlock.app.domain.finish.FinishDefinition
import com.ghostlock.app.domain.finish.FinishRepository
import com.ghostlock.app.domain.payload.PayloadDefinition
import com.ghostlock.app.domain.payload.PayloadRepository

/** Boundary that publishes a preview through the production renderer in a preview-only workspace. */
interface RootScriptPreviewPort {
    /**
     * @param payload Current canonical Payload definition.
     * @param finish Current canonical Finish definition.
     * @return Absolute path of the isolated preview script.
     */
    fun preview(payload: PayloadDefinition, finish: FinishDefinition): String
}

/** Reads both independent authorities and requests an isolated root-script preview. */
class PreviewRootScriptUseCase(
    private val payloadRepository: PayloadRepository,
    private val finishRepository: FinishRepository,
    private val previewPort: RootScriptPreviewPort,
) {
    /** @return Absolute preview path; no backend or rooted carrier is invoked. */
    operator fun invoke(): String = previewPort.preview(
        payload = payloadRepository.read(),
        finish = finishRepository.read(),
    )
}
