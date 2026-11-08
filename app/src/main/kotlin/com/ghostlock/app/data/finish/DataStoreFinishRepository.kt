package com.ghostlock.app.data.finish

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.core.DataStoreFactory
import androidx.datastore.dataStoreFile
import com.ghostlock.app.data.settings.DataStoreCommitTracker
import com.ghostlock.app.data.settings.KotlinxDataStoreSerializer
import com.ghostlock.app.domain.finish.FinishDefinition
import com.ghostlock.app.domain.finish.FinishKind
import com.ghostlock.app.domain.finish.FinishSettingsPort
import com.ghostlock.app.domain.finish.defaultFinishDefinition
import java.nio.charset.StandardCharsets
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Deferred

/** Main-process typed DataStore owner for the complete independent Finish definition. */
class DataStoreFinishRepository internal constructor(
    dataStore: DataStore<FinishDefinition>,
    scope: CoroutineScope,
) : FinishSettingsPort {
    private val commits = DataStoreCommitTracker(dataStore, scope) { }

    /** Submits one validated actor-current Finish transform. */
    override fun submit(
        transform: (FinishDefinition) -> FinishDefinition,
    ): Deferred<FinishDefinition> = commits.submit { current -> transform(current).also(::validate) }

    /** Queues an exact immutable Finish cutoff. */
    override fun snapshot(): Deferred<FinishDefinition> = commits.snapshot()

    companion object {
        private const val FILE_NAME = "finish.json"
        private val ItemIdPattern = Regex("^[a-z][a-z0-9._-]{0,63}$")

        /** Creates the sole fail-closed Finish DataStore with the new-install SELinux default. */
        fun create(context: Context, scope: CoroutineScope): DataStoreFinishRepository {
            val applicationContext = context.applicationContext
            return DataStoreFinishRepository(
                dataStore = DataStoreFactory.create(
                    serializer = KotlinxDataStoreSerializer(
                        serializer = FinishDefinition.serializer(),
                        defaultValue = defaultFinishDefinition(),
                        validate = ::validate,
                    ),
                    scope = scope,
                    produceFile = { applicationContext.dataStoreFile(FILE_NAME) },
                ),
                scope = scope,
            )
        }

        /** Validates Finish identity, action shape, argument bounds and reboot ordering. */
        private fun validate(definition: FinishDefinition) {
            require(definition.items.size <= 32) { "too many finish items" }
            val ids = HashSet<String>()
            var rebootSeen = false
            definition.items.forEachIndexed { index, item ->
                require(ItemIdPattern.matches(item.id) && ids.add(item.id)) { "invalid finish id" }
                require(item.args.size <= 32) { "too many finish args" }
                require(item.args.all { it.toByteArray(StandardCharsets.UTF_8).size <= 4096 }) {
                    "finish arg too large"
                }
                when (item.kind) {
                    FinishKind.Command -> require(item.artifactUri == null && item.args.isNotEmpty()) {
                        "command finish shape"
                    }
                    FinishKind.Script -> require(!item.artifactUri.isNullOrBlank()) {
                        "script finish shape"
                    }
                    FinishKind.Reboot -> {
                        require(!rebootSeen && index == definition.items.lastIndex) {
                            "reboot must be unique and last"
                        }
                        require(item.alwaysRun && item.artifactUri == null && item.args.isEmpty()) {
                            "reboot finish shape"
                        }
                        rebootSeen = true
                    }
                }
            }
        }
    }
}
