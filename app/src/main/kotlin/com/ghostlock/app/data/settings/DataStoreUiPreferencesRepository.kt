package com.ghostlock.app.data.settings

import android.content.Context
import android.util.Log
import androidx.datastore.core.DataStore
import androidx.datastore.core.DataStoreFactory
import androidx.datastore.core.handlers.ReplaceFileCorruptionHandler
import androidx.datastore.dataStoreFile
import com.ghostlock.app.domain.settings.UiPreferences
import com.ghostlock.app.domain.settings.UiPreferencesPort
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/** Main-process typed DataStore owner for complete application-shell preferences. */
class DataStoreUiPreferencesRepository internal constructor(
    private val dataStore: DataStore<UiPreferences>,
    scope: CoroutineScope,
) : UiPreferencesPort {
    private val mutablePreferences = MutableStateFlow(UiPreferences())
    private val commits = DataStoreCommitTracker(dataStore, scope) { value ->
        mutablePreferences.value = value
    }

    override val preferences: StateFlow<UiPreferences> = mutablePreferences.asStateFlow()

    /** Submits one actor-current transform and returns its exact committed value. */
    override fun submit(transform: (UiPreferences) -> UiPreferences): Deferred<UiPreferences> =
        commits.submit { current -> normalize(transform(current)) }

    /** Seeds the sole read-only projection before the first composition. */
    override suspend fun awaitInitial(): UiPreferences = commits.awaitInitial()

    /** Queues an exact actor-cutoff snapshot. */
    override fun snapshot(): Deferred<UiPreferences> = commits.snapshot()

    companion object {
        private const val FILE_NAME = "ui-preferences.json"
        private const val TAG = "GhostLockDataStore"

        /** Creates the only UI-preference DataStore for the main application process. */
        fun create(context: Context, scope: CoroutineScope): DataStoreUiPreferencesRepository {
            val defaultValue = UiPreferences()
            val serializer = KotlinxDataStoreSerializer(
                serializer = UiPreferences.serializer(),
                defaultValue = defaultValue,
                validate = ::validate,
            )
            return DataStoreUiPreferencesRepository(
                dataStore = DataStoreFactory.create(
                    serializer = serializer,
                    corruptionHandler = ReplaceFileCorruptionHandler { problem ->
                        Log.e(TAG, "ui preferences were corrupt; replacing with defaults", problem)
                        defaultValue
                    },
                    scope = scope,
                    produceFile = { context.applicationContext.dataStoreFile(FILE_NAME) },
                ),
                scope = scope,
            )
        }

        /** Applies bounds that were historically enforced at the command edge. */
        private fun normalize(value: UiPreferences): UiPreferences = value.copy(
            pagerInterceptionMode = value.pagerInterceptionMode.coerceIn(0, 2),
            pageScale = value.pageScale.takeIf { it.isFinite() && it > 0.0f } ?: 1.0f,
        )

        /** Rejects persisted values that cannot produce a valid renderer state. */
        private fun validate(value: UiPreferences) {
            require(value.pagerInterceptionMode in 0..2) { "pager interception mode" }
            require(value.pageScale.isFinite() && value.pageScale > 0.0f) { "page scale" }
        }
    }
}
