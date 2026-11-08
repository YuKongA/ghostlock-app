package com.ghostlock.app

import android.app.Application
import androidx.datastore.core.DataStoreFactory
import androidx.datastore.dataStoreFile
import androidx.lifecycle.viewmodel.CreationExtras
import com.ghostlock.app.data.settings.KotlinxDataStoreSerializer
import com.ghostlock.app.domain.privilege.AttackExecutorKind
import com.ghostlock.app.domain.settings.AppSelection
import com.ghostlock.app.domain.settings.OperationalPreferences
import com.ghostlock.app.domain.settings.TerminalLogLevel
import com.ghostlock.app.domain.settings.UiPreferences
import com.ghostlock.app.ui.viewmodel.SettingsViewModel
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.test.runTest
import kotlinx.serialization.KSerializer
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

/** Locks the main-process initialization barrier to exact persisted values and one lazy result. */
@RunWith(RobolectricTestRunner::class)
@Config(application = Application::class)
class AppContainerInitializationTest {
    private lateinit var application: Application

    /** Removes only this Robolectric application's five typed stores before each isolated test. */
    @Before
    fun clearTypedStores() {
        application = RuntimeEnvironment.getApplication()
        STORE_NAMES.forEach { name ->
            application.dataStoreFile(name).deleteRecursively()
        }
    }

    /** Repeated callers await one initialization and renderer inputs start from exact persisted values. */
    @Test
    fun initializeIsIdempotentAndSeedsPersistedValues() = runTest {
        val ui = UiPreferences(
            uiMode = "material",
            themeMode = 2,
            pageScale = 1.25f,
        )
        val operational = OperationalPreferences(
            logLevel = TerminalLogLevel.Error,
            showLogTimestamp = true,
            bootRetryCount = 4,
        )
        persist("ui-preferences.json", UiPreferences.serializer(), UiPreferences(), ui)
        persist(
            "operational-preferences.json",
            OperationalPreferences.serializer(),
            OperationalPreferences(),
            operational,
        )
        persist(
            "app-selection.json",
            AppSelection.serializer(),
            AppSelection(),
            AppSelection(AttackExecutorKind.APP, "DIAGNOSE"),
        )

        val container = AppContainer(application)
        container.initialize()
        container.initialize()
        val settings = container.viewModelFactory.create(
            SettingsViewModel::class.java,
            CreationExtras.Empty,
        )

        assertEquals(ui.uiMode, settings.uiState.value.uiMode)
        assertEquals(ui.themeMode, settings.uiState.value.themeMode)
        assertEquals(ui.pageScale, settings.uiState.value.pageScale)
        assertEquals(TerminalLogLevel.entries.indexOf(operational.logLevel), settings.uiState.value.selectedLogLevelIndex)
        assertTrue(settings.uiState.value.showLogTimestamp)
        assertEquals("DIAGNOSE", container.backendUiCatalog.selectedEntry()?.descriptor?.key)
    }

    /** A non-corruption storage failure terminates every waiter with the same lazy failure state. */
    @Test
    fun initializationIoFailureIsTerminal() = runTest {
        val invalidStore = application.dataStoreFile("ui-preferences.json")
        assertTrue(invalidStore.mkdirs())
        val container = AppContainer(application)

        assertTrue(runCatching { container.initialize() }.isFailure)
        assertTrue(runCatching { container.initialize() }.isFailure)
    }

    /** Writes one complete aggregate, then closes its temporary actor before AppContainer opens the file. */
    private suspend fun <T> persist(
        name: String,
        serializer: KSerializer<T>,
        defaultValue: T,
        value: T,
    ) {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        val store = DataStoreFactory.create(
            serializer = KotlinxDataStoreSerializer(serializer, defaultValue),
            scope = scope,
            produceFile = { application.dataStoreFile(name) },
        )
        store.updateData { value }
        scope.cancel()
        scope.coroutineContext[Job]?.join()
    }

    private companion object {
        val STORE_NAMES = listOf(
            "ui-preferences.json",
            "operational-preferences.json",
            "app-selection.json",
            "payload.json",
            "finish.json",
        )
    }
}
