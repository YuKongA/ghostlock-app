package com.ghostlock.app.domain.run

import com.ghostlock.app.domain.finish.FinishDefinition
import com.ghostlock.app.domain.payload.PayloadDefinition

/** Boundary that publishes a preview through the production renderer in a preview-only workspace. */
interface RootScriptPreviewPort {
    /**
     * @param payload Current canonical Payload definition.
     * @param finish Current canonical Finish definition.
     * @return Absolute path of the isolated preview script.
     */
    fun preview(payload: PayloadDefinition, finish: FinishDefinition): String
}

/** Publishes a Payload-only preview from the exact transaction result supplied by presentation. */
class PreviewRootScriptUseCase(
    private val previewPort: RootScriptPreviewPort,
) {
    /**
     * @param payload Exact immutable Payload returned by the preview-save transaction.
     * @return Absolute preview path; no Finish store, backend or rooted carrier is invoked.
     */
    operator fun invoke(payload: PayloadDefinition): String = previewPort.preview(
        payload = payload,
        finish = FinishDefinition(),
    )
}
