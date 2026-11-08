package com.ghostlock.app.data.payload

import androidx.datastore.core.DataStoreFactory
import com.ghostlock.app.data.settings.KotlinxDataStoreSerializer
import com.ghostlock.app.domain.payload.PayloadAcceptance
import com.ghostlock.app.domain.payload.PayloadDefinition
import com.ghostlock.app.domain.payload.PayloadKind
import com.ghostlock.app.domain.payload.PayloadSlot
import java.io.File
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment

/** Exact typed DataStore tests replacing the retired canonical-v3 byte contract. */
@RunWith(RobolectricTestRunner::class)
class DataStorePayloadRepositoryTest {
    @get:Rule
    val temporary = TemporaryFolder()

    /** A complete definition round-trips through the DataStore actor and cutoff handle. */
    @Test
    fun completeDefinitionRoundTrips() = runTest {
        val file = File(temporary.root, "payload.json")
        val fixture = fixture(file)
        val expected = PayloadDefinition(
            rootManager = "KernelSU Next",
            safeMode = true,
            items = listOf(
                PayloadSlot("payload-root", PayloadKind.RootManager),
                PayloadSlot(
                    id = "payload-command",
                    kind = PayloadKind.Command,
                    acceptance = PayloadAcceptance.ExitZero,
                    command = "id",
                ),
            ),
        )
        assertEquals(expected, fixture.repository.submit { expected }.await())
        assertEquals(expected, fixture.repository.snapshot().await())
        fixture.close()
        val reopened = fixture(file)
        assertEquals(expected, reopened.repository.snapshot().await())
        reopened.close()
    }

    /** Invalid stable identities fail the accepted transaction instead of becoming authority. */
    @Test
    fun invalidDefinitionFailsClosed() = runTest {
        val fixture = fixture("invalid.json")
        val invalid = PayloadDefinition(items = listOf(PayloadSlot("BAD ID", PayloadKind.RootManager)))
        assertTrue(runCatching { fixture.repository.submit { invalid }.await() }.isFailure)
        fixture.close()
    }

    /** Malformed persisted JSON cannot silently become the default executable Payload. */
    @Test
    fun malformedJsonFailsClosed() = runTest {
        val file = File(temporary.root, "corrupt.json").apply { writeText("{not-json") }
        val fixture = fixture(file)
        assertTrue(runCatching { fixture.repository.snapshot().await() }.isFailure)
        fixture.close()
    }

    private fun fixture(name: String): Fixture = fixture(File(temporary.root, name))

    private fun fixture(file: File): Fixture {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        val store = DataStoreFactory.create(
            serializer = KotlinxDataStoreSerializer(
                serializer = PayloadDefinition.serializer(),
                defaultValue = PayloadDefinition(),
            ),
            scope = scope,
            produceFile = { file },
        )
        return Fixture(
            repository = DataStorePayloadRepository(
                context = RuntimeEnvironment.getApplication(),
                dataStore = store,
                scope = scope,
            ),
            scope = scope,
        )
    }

    private data class Fixture(
        val repository: DataStorePayloadRepository,
        val scope: CoroutineScope,
    ) {
        suspend fun close() {
            scope.cancel()
            scope.coroutineContext[Job]?.join()
        }
    }
}
