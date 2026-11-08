package com.ghostlock.app.data.settings

import androidx.datastore.core.DataStoreFactory
import androidx.datastore.core.Serializer
import com.ghostlock.app.domain.settings.UiPreferences
import java.io.File
import java.io.InputStream
import java.io.OutputStream
import java.util.concurrent.atomic.AtomicBoolean
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/** Covers exact initial seeding and actor-current field preservation. */
class DataStoreUiPreferencesRepositoryTest {
    @get:Rule
    val temporary = TemporaryFolder()

    /** Initial projection is seeded with the exact non-default persisted value. */
    @Test
    fun awaitInitialSeedsPersistedValue() = runTest {
        val fixture = fixture()
        val persisted = UiPreferences(uiMode = "material", themeMode = 6, pageScale = 1.2f)
        fixture.store.updateData { persisted }
        assertEquals(persisted, fixture.repository.awaitInitial())
        assertEquals(persisted, fixture.repository.preferences.value)
        fixture.close()
    }

    /** Two accepted field transforms preserve both values through the sole actor. */
    @Test
    fun concurrentFieldsDoNotLoseEachOther() = runTest {
        val fixture = fixture()
        fixture.repository.awaitInitial()
        val first = fixture.repository.submit { it.copy(checkUpdate = false) }
        val second = fixture.repository.submit { it.copy(enableBlur = true) }
        first.await()
        val result = second.await()
        assertEquals(false, result.checkUpdate)
        assertEquals(true, result.enableBlur)
        fixture.close()
    }

    /** A second command is accepted while the first disk write is blocked and both transforms survive. */
    @Test
    fun blockedFirstWriteDoesNotRejectSecondTransform() = runTest {
        val started = CompletableDeferred<Unit>()
        val release = CompletableDeferred<Unit>()
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        val store = DataStoreFactory.create(
            serializer = BlockingUiSerializer(started, release),
            scope = scope,
            produceFile = { File(temporary.root, "blocked-ui.json") },
        )
        val repository = DataStoreUiPreferencesRepository(store, scope)
        repository.awaitInitial()
        val first = repository.submit { it.copy(checkUpdate = false) }
        started.await()
        val second = repository.submit { it.copy(enableBlur = true) }
        assertEquals(false, second.isCompleted)
        release.complete(Unit)

        first.await()
        val result = second.await()
        assertEquals(false, result.checkUpdate)
        assertEquals(true, result.enableBlur)
        scope.cancel()
    }

    private fun fixture(): Fixture {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        val store = DataStoreFactory.create(
            serializer = KotlinxDataStoreSerializer(UiPreferences.serializer(), UiPreferences()),
            scope = scope,
            produceFile = { File(temporary.root, "ui.json") },
        )
        return Fixture(store, DataStoreUiPreferencesRepository(store, scope), scope)
    }

    private data class Fixture(
        val store: androidx.datastore.core.DataStore<UiPreferences>,
        val repository: DataStoreUiPreferencesRepository,
        val scope: CoroutineScope,
    ) {
        fun close() = scope.cancel()
    }

    /** Test serializer that stalls exactly the first durable write without adding another repository queue. */
    private class BlockingUiSerializer(
        private val started: CompletableDeferred<Unit>,
        private val release: CompletableDeferred<Unit>,
    ) : Serializer<UiPreferences> {
        private val delegate = KotlinxDataStoreSerializer(UiPreferences.serializer(), UiPreferences())
        private val firstWrite = AtomicBoolean(true)

        override val defaultValue: UiPreferences = UiPreferences()

        /** Delegates exact reads to the production serializer. */
        override suspend fun readFrom(input: InputStream): UiPreferences = delegate.readFrom(input)

        /** Holds the first write until the second actor-current transform has been submitted. */
        override suspend fun writeTo(t: UiPreferences, output: OutputStream) {
            if (firstWrite.compareAndSet(true, false)) {
                started.complete(Unit)
                release.await()
            }
            delegate.writeTo(t, output)
        }
    }
}
