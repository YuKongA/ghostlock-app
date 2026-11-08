package com.ghostlock.app.data.settings

import android.content.Context
import android.util.Log
import androidx.datastore.core.DataStore
import androidx.datastore.core.DataStoreFactory
import androidx.datastore.core.handlers.ReplaceFileCorruptionHandler
import androidx.datastore.dataStoreFile
import com.ghostlock.app.domain.settings.AppSelection
import com.ghostlock.app.domain.settings.AppSelectionPort
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/** Main-process typed DataStore owner for executor and backend selections. */
class DataStoreAppSelectionRepository internal constructor(
    dataStore: DataStore<AppSelection>,
    scope: CoroutineScope,
) : AppSelectionPort {
    private val mutableSelection = MutableStateFlow(AppSelection())
    private val commits = DataStoreCommitTracker(dataStore, scope) { value ->
        mutableSelection.value = value
    }

    override val selection: StateFlow<AppSelection> = mutableSelection.asStateFlow()

    /** Submits one field-local transform without replacing unrelated selection state. */
    override fun submit(transform: (AppSelection) -> AppSelection): Deferred<AppSelection> =
        commits.submit { current -> validateAndReturn(transform(current)) }

    /** Seeds the committed projection before privilege/backend restoration. */
    override suspend fun awaitInitial(): AppSelection = commits.awaitInitial()

    /** Queues an immutable actor-cutoff snapshot. */
    override fun snapshot(): Deferred<AppSelection> = commits.snapshot()

    companion object {
        private const val FILE_NAME = "app-selection.json"
        private const val TAG = "GhostLockDataStore"

        /** Creates the sole application-selection DataStore. */
        fun create(context: Context, scope: CoroutineScope): DataStoreAppSelectionRepository {
            val defaultValue = AppSelection()
            return DataStoreAppSelectionRepository(
                dataStore = DataStoreFactory.create(
                    serializer = KotlinxDataStoreSerializer(
                        serializer = AppSelection.serializer(),
                        defaultValue = defaultValue,
                        validate = ::validate,
                    ),
                    corruptionHandler = ReplaceFileCorruptionHandler { problem ->
                        Log.e(TAG, "application selection was corrupt; replacing with defaults", problem)
                        defaultValue
                    },
                    scope = scope,
                    produceFile = { context.applicationContext.dataStoreFile(FILE_NAME) },
                ),
                scope = scope,
            )
        }

        /** Returns one already-validated aggregate for use inside a transaction. */
        private fun validateAndReturn(value: AppSelection): AppSelection = value.also(::validate)

        /** Rejects blank or path-like backend identities while allowing an unset selection. */
        private fun validate(value: AppSelection) {
            val key = value.selectedBackendKey ?: return
            require(key.isNotBlank() && '/' !in key && '\u0000' !in key) { "backend key" }
        }
    }
}
