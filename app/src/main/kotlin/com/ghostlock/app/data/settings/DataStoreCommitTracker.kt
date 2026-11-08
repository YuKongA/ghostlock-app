package com.ghostlock.app.data.settings

import androidx.datastore.core.DataStore
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.first

/**
 * Registers accepted DataStore transactions and exposes exact actor-cutoff snapshots.
 *
 * @param dataStore Sole writer and ordering authority.
 * @param scope Application-lifetime scope; ViewModel cancellation must not discard accepted intent.
 * @param onCommitted Publishes only values durably returned by DataStore.
 * @note This class never stores an aggregate, reorders, retries or replays writes; it is not a second writer queue.
 */
class DataStoreCommitTracker<T>(
    private val dataStore: DataStore<T>,
    private val scope: CoroutineScope,
    private val onCommitted: (T) -> Unit,
) {
    private val lock = Any()
    private var firstFailure: Throwable? = null

    /**
     * Accepts one transform before returning to the UI callback.
     *
     * @param transform Pure transform executed against the actor's current value.
     * @return Handle containing the exact immutable aggregate committed by this transaction.
     */
    fun submit(transform: (T) -> T): Deferred<T> = submitOperation { store ->
        store.updateData { current ->
            throwRecordedFailure()
            transform(current)
        }
    }

    /**
     * Places an identity transaction in the same actor at the current callback cutoff.
     *
     * @return Exact aggregate at that actor position; later accepted writes cannot change it.
     */
    fun snapshot(): Deferred<T> = submit { current -> current }

    /**
     * Runs an accepted application-scope operation and tracks its terminal result.
     *
     * @param operation Operation that must enter [DataStore.updateData] before its caller yields.
     * @return Completion handle registered before the operation starts.
     */
    fun submitOperation(operation: suspend (DataStore<T>) -> T): Deferred<T> {
        return scope.async(start = CoroutineStart.UNDISPATCHED) {
            try {
                val committed = operation(dataStore)
                onCommitted(committed)
                committed
            } catch (problem: Throwable) {
                synchronized(lock) {
                    if (firstFailure == null) firstFailure = problem
                }
                throw problem
            }
        }
    }

    /**
     * Reads the initial store value before any renderer is constructed.
     *
     * @return Exact persisted aggregate or the serializer default for a missing store.
     */
    suspend fun awaitInitial(): T {
        val value = dataStore.data.first()
        onCommitted(value)
        return value
    }

    /** Throws the first accepted write failure so later preview/run transactions fail closed. */
    private fun throwRecordedFailure() {
        val failure = synchronized(lock) { firstFailure }
        if (failure != null) throw IllegalStateException("an accepted DataStore transaction failed", failure)
    }
}
