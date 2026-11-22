package com.ghostlock.app.data.settings

import android.content.Context
import android.util.Log
import androidx.datastore.core.CorruptionException
import androidx.datastore.core.DataStore
import androidx.datastore.core.DataStoreFactory
import androidx.datastore.core.Serializer
import androidx.datastore.core.handlers.ReplaceFileCorruptionHandler
import androidx.datastore.dataStoreFile
import com.ghostlock.app.domain.settings.AppSelection
import com.ghostlock.app.domain.settings.AppSelectionPort
import com.ghostlock.app.domain.settings.BackendSelectionState
import java.io.InputStream
import java.io.OutputStream
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.serialization.SerializationException
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive

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
        commits.submit(transform)

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
                    serializer = AppSelectionSerializer(defaultValue),
                    corruptionHandler = ReplaceFileCorruptionHandler { problem ->
                        Log.e(TAG, "application selection was corrupt; requiring a new backend choice", problem)
                        AppSelection(backendSelectionState = BackendSelectionState.NEEDS_USER_CHOICE)
                    },
                    scope = scope,
                    produceFile = { context.applicationContext.dataStoreFile(FILE_NAME) },
                ),
                scope = scope,
            )
        }
    }
}

/** Strict serializer with one bounded migration for the historical shape that lacked selection state. */
internal class AppSelectionSerializer(
    override val defaultValue: AppSelection,
) : Serializer<AppSelection> {
    override suspend fun readFrom(input: InputStream): AppSelection = try {
        val parsed = JSON.parseToJsonElement(input.readBytes().decodeToString()) as? JsonObject
            ?: throw IllegalArgumentException("application selection must be a JSON object")
        val normalized = when (parsed.keys) {
            CURRENT_FIELDS -> parsed
            LEGACY_FIELDS -> {
                val selected = requireNotNull(parsed["selectedBackendKey"]) {
                    "legacy selectedBackendKey is missing"
                }
                JsonObject(
                    parsed + (
                        "backendSelectionState" to JsonPrimitive(
                            if (selected is JsonNull) {
                                BackendSelectionState.FRESH_UNINITIALIZED.name
                            } else {
                                BackendSelectionState.SELECTED.name
                            },
                        )
                    ),
                )
            }
            else -> throw IllegalArgumentException("application selection fields are invalid")
        }
        JSON.decodeFromJsonElement(AppSelection.serializer(), normalized)
    } catch (problem: CorruptionException) {
        throw problem
    } catch (problem: SerializationException) {
        throw CorruptionException("application selection JSON is malformed", problem)
    } catch (problem: IllegalArgumentException) {
        throw CorruptionException("application selection violates its domain contract", problem)
    }

    override suspend fun writeTo(t: AppSelection, output: OutputStream) {
        output.write(JSON.encodeToString(AppSelection.serializer(), t).encodeToByteArray())
    }

    private companion object {
        val LEGACY_FIELDS = setOf("selectedExecutor", "selectedBackendKey")
        val CURRENT_FIELDS = LEGACY_FIELDS + "backendSelectionState"
        val JSON = Json {
            encodeDefaults = true
            ignoreUnknownKeys = false
            explicitNulls = true
            isLenient = false
            allowSpecialFloatingPointValues = false
        }
    }
}
