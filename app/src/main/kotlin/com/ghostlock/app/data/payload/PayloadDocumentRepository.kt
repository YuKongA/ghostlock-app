package com.ghostlock.app.data.payload

import android.content.Context
import com.ghostlock.app.domain.payload.PayloadDefinition
import com.ghostlock.app.domain.payload.PayloadDocumentV3
import com.ghostlock.app.domain.payload.PayloadEditorPort
import com.ghostlock.app.domain.payload.PayloadRootManagerOption
import com.ghostlock.app.domain.payload.PayloadRootManagers
import java.io.File

/**
 * File-backed version-three Payload repository and Android manager-environment adapter.
 *
 * @param context Application context used only for the private document path and package facts.
 */
class PayloadDocumentRepository(
    context: Context,
) : PayloadEditorPort {
    private val applicationContext = context.applicationContext
    private val documentFile = File(File(applicationContext.filesDir, "definitions"), "payload-v3.json")

    /** @return Decoded canonical document, or the deterministic default before first save. */
    override fun read(): PayloadDefinition {
        if (!documentFile.exists()) return PayloadDefinition()
        return PayloadDocumentCodec.decode(documentFile.readBytes()).definition
    }

    /** Publishes the only persisted Payload authority as canonical version-three JSON. */
    override fun save(definition: PayloadDefinition) {
        val bytes = PayloadDocumentCodec.encode(PayloadDocumentV3(definition = definition))
        CanonicalDocumentFile.write(documentFile, bytes)
    }

    /** @return Current package-installation projection without persisting environment state. */
    override fun rootManagerOptions(): List<PayloadRootManagerOption> = PayloadRootManagers.all.map { manager ->
        val installed = manager.packages.firstOrNull { packageName ->
            runCatching { applicationContext.packageManager.getPackageInfo(packageName, 0) }.isSuccess
        }
        PayloadRootManagerOption(
            name = manager.name,
            installedPackage = installed,
            iconPackage = installed ?: manager.packages.firstOrNull().orEmpty(),
            launchOnly = manager.launchOnly,
        )
    }
}
