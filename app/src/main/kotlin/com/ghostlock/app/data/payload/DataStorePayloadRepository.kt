package com.ghostlock.app.data.payload

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.core.DataStoreFactory
import androidx.datastore.dataStoreFile
import com.ghostlock.app.data.settings.DataStoreCommitTracker
import com.ghostlock.app.data.settings.KotlinxDataStoreSerializer
import com.ghostlock.app.domain.payload.PayloadDefinition
import com.ghostlock.app.domain.payload.PayloadEditorPort
import com.ghostlock.app.domain.payload.PayloadKind
import com.ghostlock.app.domain.payload.PayloadRootManagerOption
import com.ghostlock.app.domain.payload.PayloadRootManagers
import com.ghostlock.app.domain.payload.defaultAcceptance
import java.nio.charset.StandardCharsets
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Deferred

/** Main-process typed DataStore owner for the complete independent Payload definition. */
class DataStorePayloadRepository internal constructor(
    private val context: Context,
    dataStore: DataStore<PayloadDefinition>,
    scope: CoroutineScope,
) : PayloadEditorPort {
    private val commits = DataStoreCommitTracker(dataStore, scope) { }

    /** Submits one validated actor-current Payload transform. */
    override fun submit(
        transform: (PayloadDefinition) -> PayloadDefinition,
    ): Deferred<PayloadDefinition> = commits.submit { current -> transform(current).also(::validate) }

    /** Queues an exact immutable Payload cutoff. */
    override fun snapshot(): Deferred<PayloadDefinition> = commits.snapshot()

    /** @return Current package-installation facts without persisting environment state. */
    override fun rootManagerOptions(): List<PayloadRootManagerOption> = PayloadRootManagers.all.map { manager ->
        val installed = manager.packages.firstOrNull { packageName ->
            runCatching { context.packageManager.getPackageInfo(packageName, 0) }.isSuccess
        }
        PayloadRootManagerOption(
            name = manager.name,
            installedPackage = installed,
            iconPackage = installed ?: manager.packages.firstOrNull().orEmpty(),
            launchOnly = manager.launchOnly,
        )
    }

    companion object {
        private const val FILE_NAME = "payload.json"
        private val ItemIdPattern = Regex("^[a-z][a-z0-9._-]{0,63}$")

        /** Creates the sole fail-closed Payload DataStore without a replacement corruption handler. */
        fun create(context: Context, scope: CoroutineScope): DataStorePayloadRepository {
            val applicationContext = context.applicationContext
            return DataStorePayloadRepository(
                context = applicationContext,
                dataStore = DataStoreFactory.create(
                    serializer = KotlinxDataStoreSerializer(
                        serializer = PayloadDefinition.serializer(),
                        defaultValue = PayloadDefinition(),
                        validate = ::validate,
                    ),
                    scope = scope,
                    produceFile = { applicationContext.dataStoreFile(FILE_NAME) },
                ),
                scope = scope,
            )
        }

        /** Validates complete Payload identity, shape and bounded text fields. */
        private fun validate(definition: PayloadDefinition) {
            require(PayloadRootManagers.find(definition.rootManager) != null) { "unknown root manager" }
            require(definition.items.size <= 64) { "too many payload items" }
            val ids = HashSet<String>()
            definition.items.forEach { item ->
                require(ItemIdPattern.matches(item.id) && ids.add(item.id)) { "invalid payload id" }
                require(item.acceptance == item.kind.defaultAcceptance()) { "payload acceptance mismatch" }
                require(item.scriptMode == 0 || item.scriptMode == 1) { "invalid script mode" }
                require(item.command.toByteArray(StandardCharsets.UTF_8).size <= 4096) {
                    "payload command too large"
                }
                require(item.lkmParams.toByteArray(StandardCharsets.UTF_8).size <= 4096) {
                    "payload args too large"
                }
                when (item.kind) {
                    PayloadKind.RootManager -> require(
                        item.scriptMode == 0 && item.command.isEmpty() &&
                            item.scriptUri == null && item.lkmUri == null,
                    ) { "root-manager payload shape" }
                    PayloadKind.Command -> require(item.lkmUri == null && item.lkmParams.isEmpty()) {
                        "command payload shape"
                    }
                    PayloadKind.Lkm -> require(
                        item.scriptMode == 0 && item.command.isEmpty() && item.scriptUri == null,
                    ) { "lkm payload shape" }
                }
            }
        }
    }
}
