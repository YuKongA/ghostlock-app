package com.ghostlock.app.data.settings

import androidx.datastore.core.DataStoreFactory
import com.ghostlock.app.domain.privilege.AttackExecutorKind
import com.ghostlock.app.domain.settings.AppSelection
import com.ghostlock.app.domain.settings.BackendSelectionState
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/** Ensures the combined selection aggregate preserves independent fields. */
class DataStoreAppSelectionRepositoryTest {
    @get:Rule
    val temporary = TemporaryFolder()

    /** Executor and backend transforms preserve each other in either accepted order. */
    @Test
    fun independentFieldsPreserveEachOther() = runTest {
        val file = File(temporary.root, "selection.json")
        val fixture = fixture(file)
        fixture.repository.awaitInitial()
        val executor = fixture.repository.submit { it.copy(selectedExecutor = AttackExecutorKind.SHIZUKU) }
        val backend = fixture.repository.submit {
            it.copy(
                backendSelectionState = BackendSelectionState.SELECTED,
                selectedBackendKey = "DIAGNOSE",
            )
        }
        executor.await()
        val result = backend.await()
        assertEquals(AttackExecutorKind.SHIZUKU, result.selectedExecutor)
        assertEquals("DIAGNOSE", result.selectedBackendKey)
        fixture.close()

        val reopened = fixture(file)
        val persisted = reopened.repository.awaitInitial()
        assertEquals(AttackExecutorKind.SHIZUKU, persisted.selectedExecutor)
        assertEquals("DIAGNOSE", persisted.selectedBackendKey)
        reopened.close()
    }

    /** Historical files without state migrate once from null/key semantics into the explicit state. */
    @Test
    fun legacyMissingStateMigratesToExplicitSelectionState() = runTest {
        val serializer = AppSelectionSerializer(AppSelection())
        val selected = serializer.readFrom(
            ByteArrayInputStream(
                """{"selectedExecutor":"APP","selectedBackendKey":"DIAGNOSE"}""".encodeToByteArray(),
            ),
        )
        assertEquals(BackendSelectionState.SELECTED, selected.backendSelectionState)
        assertEquals("DIAGNOSE", selected.selectedBackendKey)

        val fresh = serializer.readFrom(
            ByteArrayInputStream(
                """{"selectedExecutor":"SHIZUKU","selectedBackendKey":null}""".encodeToByteArray(),
            ),
        )
        assertEquals(BackendSelectionState.FRESH_UNINITIALIZED, fresh.backendSelectionState)
        assertEquals(null, fresh.selectedBackendKey)

        val output = ByteArrayOutputStream()
        serializer.writeTo(selected, output)
        assertEquals(selected, serializer.readFrom(ByteArrayInputStream(output.toByteArray())))
    }

    /** Creates one isolated selection store whose actor lifetime is explicit in the test. */
    private fun fixture(file: File): Fixture {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        val store = DataStoreFactory.create(
            serializer = AppSelectionSerializer(AppSelection()),
            scope = scope,
            produceFile = { file },
        )
        return Fixture(DataStoreAppSelectionRepository(store, scope), scope)
    }

    /** Repository plus the application-like scope that owns its DataStore actor. */
    private data class Fixture(
        val repository: DataStoreAppSelectionRepository,
        val scope: CoroutineScope,
    ) {
        suspend fun close() {
            scope.cancel()
            scope.coroutineContext[Job]?.join()
        }
    }
}
