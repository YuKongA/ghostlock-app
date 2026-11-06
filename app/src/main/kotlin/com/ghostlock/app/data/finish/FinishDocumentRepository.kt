package com.ghostlock.app.data.finish

import android.content.Context
import com.ghostlock.app.data.payload.CanonicalDocumentFile
import com.ghostlock.app.domain.finish.FinishDefinition
import com.ghostlock.app.domain.finish.FinishDocumentV3
import com.ghostlock.app.domain.finish.FinishSettingsPort
import java.io.File

/**
 * File-backed Finish repository; it never mirrors state into SharedPreferences or a backend profile.
 *
 * @param context Application context used for the private canonical document path.
 */
class FinishDocumentRepository(
    context: Context,
) : FinishSettingsPort {
    private val documentFile = File(File(context.applicationContext.filesDir, "definitions"), "finish-v3.json")

    /** @return Decoded canonical definition, or the neutral empty definition before first save. */
    override fun read(): FinishDefinition {
        if (!documentFile.exists()) return FinishDefinition()
        return FinishDocumentCodec.decode(documentFile.readBytes()).definition
    }

    /** Publishes the only persisted Finish authority as canonical version-three JSON. */
    override fun save(definition: FinishDefinition) {
        val bytes = FinishDocumentCodec.encode(FinishDocumentV3(definition = definition))
        CanonicalDocumentFile.write(documentFile, bytes)
    }
}
