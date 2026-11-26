package com.ghostlock.app.data.settings

import android.content.Context
import android.content.ContextWrapper
import androidx.datastore.dataStoreFile
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ghostlock.app.domain.settings.OperationalPreferences
import java.io.File
import java.util.UUID
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Real Android typed DataStore coverage for defaults, normalization and picker publication. */
@RunWith(AndroidJUnit4::class)
class TypedDataStorePersistenceTest {
    private lateinit var context: Context
    private lateinit var storeDirectory: File
    private var scope: CoroutineScope? = null

    @Before
    fun clearStore() {
        val application = ApplicationProvider.getApplicationContext<Context>()
        storeDirectory = File(application.cacheDir, "typed-datastore-${UUID.randomUUID()}")
        check(storeDirectory.mkdirs())
        // Exercise the production factory without sharing the live application's DataStore file.
        context = object : ContextWrapper(application) {
            override fun getApplicationContext(): Context = this
            override fun getFilesDir(): File = storeDirectory
        }
    }

    @After
    fun closeStore() {
        runBlocking { stopStoreActor() }
        storeDirectory.deleteRecursively()
    }

    /** Missing typed store retains automatic export and the historical Downloads directory. */
    @Test
    fun missingStoreUsesDomainDefaults() = runBlocking {
        val repository = repository()
        val value = repository.awaitInitial()
        assertTrue(value.autoExportLogs)
        assertEquals(OperationalPreferences.DEFAULT_LOG_EXPORT_LOCATION, value.logExportLocation)
    }

    /** Explicit false and normalized paths persist as one complete aggregate. */
    @Test
    fun completeAggregateNormalizesAndPersists() = runBlocking {
        val expected = OperationalPreferences(
            autoExportLogs = false,
            logExportLocation = "Download/custom/logs",
        )
        val repository = repository()
        repository.awaitInitial()
        val value = repository.submit { current ->
            current.copy(autoExportLogs = false, logExportLocation = "Download//custom//logs")
        }.await()
        assertEquals(expected, value)
        assertTrue(storeFile().isFile)

        stopStoreActor()
        assertEquals(expected, repository().awaitInitial())
    }

    /** Picker accepts only a safe primary Downloads tree and leaves state unchanged on rejection. */
    @Test
    fun pickerAcceptsOnlyPrimaryDownloads() = runBlocking {
        val expected = OperationalPreferences(logExportLocation = "Download/custom")
        val repository = repository()
        repository.awaitInitial()
        val accepted = "content://com.android.externalstorage.documents/tree/primary%3ADownload%2Fcustom"
        val rejected = "content://com.android.externalstorage.documents/tree/primary%3APictures"
        assertTrue(repository.selectLogExportDirectory(accepted).await())
        assertEquals(expected, repository.preferences.value)

        stopStoreActor()
        val reopened = repository()
        assertEquals(expected, reopened.awaitInitial())
        assertFalse(reopened.selectLogExportDirectory(rejected).await())
        assertEquals(expected, reopened.preferences.value)

        stopStoreActor()
        assertEquals(expected, repository().awaitInitial())
    }

    /** A malformed display-only store is replaced by the domain default instead of blocking startup. */
    @Test
    fun malformedOperationalStoreFallsBackToDefault() = runBlocking {
        storeFile().parentFile?.mkdirs()
        storeFile().writeText("{not-json")
        val repository = repository()
        val value = repository.awaitInitial()
        assertTrue(value.autoExportLogs)
        assertEquals(OperationalPreferences.DEFAULT_LOG_EXPORT_LOCATION, value.logExportLocation)
        assertFalse(storeFile().readText().contains("not-json"))
    }

    private suspend fun stopStoreActor() {
        val activeScope = scope ?: return
        activeScope.cancel()
        activeScope.coroutineContext[Job]?.join()
        scope = null
    }

    private fun repository(): DataStoreOperationalPreferencesRepository {
        check(scope == null) { "Previous DataStore actor must stop before reopening" }
        val newScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        scope = newScope
        return DataStoreOperationalPreferencesRepository.create(context, newScope)
    }

    private fun storeFile(): File = context.dataStoreFile("operational-preferences.json")
}
