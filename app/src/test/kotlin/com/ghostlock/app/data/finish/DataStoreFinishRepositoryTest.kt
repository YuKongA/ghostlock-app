package com.ghostlock.app.data.finish

import androidx.datastore.core.DataStoreFactory
import com.ghostlock.app.data.settings.KotlinxDataStoreSerializer
import com.ghostlock.app.domain.finish.FinishDefinition
import com.ghostlock.app.domain.finish.FinishItemDefinition
import com.ghostlock.app.domain.finish.FinishKind
import com.ghostlock.app.domain.finish.FinishSeverity
import com.ghostlock.app.domain.finish.RESTORE_SELINUX_FINISH_ID
import com.ghostlock.app.domain.finish.defaultFinishDefinition
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

/** Exact typed DataStore tests replacing the retired Finish-v3 byte contract. */
class DataStoreFinishRepositoryTest {
    @get:Rule
    val temporary = TemporaryFolder()

    /** Missing new store starts with the independent SELinux restoration action enabled. */
    @Test
    fun missingStoreUsesRestoreSelinuxDefault() = runTest {
        val fixture = fixture("default.json")
        val value = fixture.repository.snapshot().await()
        assertTrue(value.items.any { it.id == RESTORE_SELINUX_FINISH_ID })
        fixture.close()
    }

    /** Complete ordered actions round-trip without a version wrapper. */
    @Test
    fun completeDefinitionRoundTrips() = runTest {
        val file = File(temporary.root, "finish.json")
        val fixture = fixture(file)
        val expected = FinishDefinition(
            listOf(
                FinishItemDefinition(
                    id = "finish-command",
                    kind = FinishKind.Command,
                    severity = FinishSeverity.Normal,
                    alwaysRun = false,
                    args = listOf("/system/bin/true"),
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

    /** A non-final reboot fails closed inside the accepted transaction. */
    @Test
    fun invalidRebootOrderingFailsClosed() = runTest {
        val fixture = fixture("invalid.json")
        val reboot = FinishItemDefinition(
            id = "finish-reboot",
            kind = FinishKind.Reboot,
            severity = FinishSeverity.SafetyCritical,
            alwaysRun = true,
        )
        val command = FinishItemDefinition(
            id = "finish-command",
            kind = FinishKind.Command,
            severity = FinishSeverity.Normal,
            alwaysRun = false,
            args = listOf("true"),
        )
        assertTrue(
            runCatching {
                fixture.repository.submit { FinishDefinition(listOf(reboot, command)) }.await()
            }.isFailure,
        )
        fixture.close()
    }

    /** Malformed persisted JSON cannot silently become an empty or default Finish authority. */
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
                serializer = FinishDefinition.serializer(),
                defaultValue = defaultFinishDefinition(),
            ),
            scope = scope,
            produceFile = { file },
        )
        return Fixture(DataStoreFinishRepository(store, scope), scope)
    }

    private data class Fixture(
        val repository: DataStoreFinishRepository,
        val scope: CoroutineScope,
    ) {
        suspend fun close() {
            scope.cancel()
            scope.coroutineContext[Job]?.join()
        }
    }
}
