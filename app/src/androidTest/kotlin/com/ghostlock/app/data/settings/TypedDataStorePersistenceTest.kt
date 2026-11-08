package com.ghostlock.app.data.settings

import android.content.Context
import androidx.datastore.dataStoreFile
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ghostlock.app.domain.settings.OperationalPreferences
import java.io.File
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
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
    private var scope: CoroutineScope? = null

    @Before
    fun clearStore() {
        context = ApplicationProvider.getApplicationContext()
        storeFile().delete()
    }

    @After
    fun closeStore() {
        scope?.cancel()
        scope = null
        storeFile().delete()
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
        val repository = repository()
        repository.awaitInitial()
        val value = repository.submit { current ->
            current.copy(autoExportLogs = false, logExportLocation = "Download//custom//logs")
        }.await()
        assertFalse(value.autoExportLogs)
        assertEquals("Download/custom/logs", value.logExportLocation)
        assertTrue(storeFile().isFile)
    }

    /** Picker accepts only a safe primary Downloads tree and leaves state unchanged on rejection. */
    @Test
    fun pickerAcceptsOnlyPrimaryDownloads() = runBlocking {
        val repository = repository()
        repository.awaitInitial()
        val accepted = "content://com.android.externalstorage.documents/tree/primary%3ADownload%2Fcustom"
        val rejected = "content://com.android.externalstorage.documents/tree/primary%3APictures"
        assertTrue(repository.selectLogExportDirectory(accepted).await())
        assertEquals("Download/custom", repository.preferences.value.logExportLocation)
        assertFalse(repository.selectLogExportDirectory(rejected).await())
        assertEquals("Download/custom", repository.preferences.value.logExportLocation)
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

    private fun repository(): DataStoreOperationalPreferencesRepository {
        val newScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        scope = newScope
        return DataStoreOperationalPreferencesRepository.create(context, newScope)
    }

    private fun storeFile(): File = context.dataStoreFile("operational-preferences.json")
}
