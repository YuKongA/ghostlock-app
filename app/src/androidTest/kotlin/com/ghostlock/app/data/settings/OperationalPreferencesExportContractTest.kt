package com.ghostlock.app.data.settings

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ghostlock.app.domain.settings.OperationalPreferences
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Locks historical defaults, path normalization, and Downloads tree-picker rejection semantics. */
@RunWith(AndroidJUnit4::class)
class OperationalPreferencesExportContractTest {
    private lateinit var context: Context

    @Before
    fun clearStores() {
        context = ApplicationProvider.getApplicationContext()
        context.getSharedPreferences(LOG_STORE, Context.MODE_PRIVATE).edit().clear().commit()
        context.getSharedPreferences(ATTACK_STORE, Context.MODE_PRIVATE).edit().clear().commit()
        context.getSharedPreferences(BOOT_STORE, Context.MODE_PRIVATE).edit().clear().commit()
    }

    @After
    fun clearStoresAfterTest() {
        context.getSharedPreferences(LOG_STORE, Context.MODE_PRIVATE).edit().clear().commit()
        context.getSharedPreferences(ATTACK_STORE, Context.MODE_PRIVATE).edit().clear().commit()
        context.getSharedPreferences(BOOT_STORE, Context.MODE_PRIVATE).edit().clear().commit()
    }

    /** Missing historical keys retain automatic export enabled and the original Downloads folder. */
    @Test
    fun absentKeysUseHistoricalDefaults() {
        val adapter = SharedPreferencesOperationalPreferencesAdapter(context)

        assertTrue(adapter.preferences.value.autoExportLogs)
        assertEquals(
            OperationalPreferences.DEFAULT_LOG_EXPORT_LOCATION,
            adapter.preferences.value.logExportLocation,
        )
    }

    /** A stored false value is not confused with an absent key. */
    @Test
    fun storedFalseRemainsFalse() {
        context.getSharedPreferences(LOG_STORE, Context.MODE_PRIVATE)
            .edit()
            .putBoolean(KEY_AUTO_EXPORT, false)
            .commit()

        val adapter = SharedPreferencesOperationalPreferencesAdapter(context)

        assertFalse(adapter.preferences.value.autoExportLogs)
    }

    /** Save normalizes bare/repeated paths and rejects empty or traversal paths to the default. */
    @Test
    fun savedPathsUseOneDownloadsAuthority() {
        val adapter = SharedPreferencesOperationalPreferencesAdapter(context)

        adapter.save(adapter.preferences.value.copy(logExportLocation = "foo"))
        assertEquals("Download/foo", adapter.preferences.value.logExportLocation)

        adapter.save(adapter.preferences.value.copy(logExportLocation = "Download//ghostlock-log//x"))
        assertEquals("Download/ghostlock-log/x", adapter.preferences.value.logExportLocation)

        adapter.save(adapter.preferences.value.copy(logExportLocation = "Download/../escape"))
        assertEquals(
            OperationalPreferences.DEFAULT_LOG_EXPORT_LOCATION,
            adapter.preferences.value.logExportLocation,
        )

        adapter.save(adapter.preferences.value.copy(logExportLocation = ""))
        assertEquals(
            OperationalPreferences.DEFAULT_LOG_EXPORT_LOCATION,
            adapter.preferences.value.logExportLocation,
        )
    }

    /** Picker accepts only primary-storage Downloads and leaves the prior value unchanged on rejection. */
    @Test
    fun pickerAcceptsOnlyPrimaryDownloads() {
        val adapter = SharedPreferencesOperationalPreferencesAdapter(context)
        val accepted = "content://com.android.externalstorage.documents/tree/primary%3ADownload%2Fcustom"
        val rejected = listOf(
            "content://com.android.externalstorage.documents/tree/primary%3A",
            "content://com.android.externalstorage.documents/tree/primary%3APictures",
            "content://com.android.externalstorage.documents/tree/primary%3ADownload%2F.",
            "content://com.android.externalstorage.documents/tree/primary%3ADownload%2F..",
        )

        assertTrue(adapter.selectLogExportDirectory(accepted))
        assertEquals("Download/custom", adapter.preferences.value.logExportLocation)
        rejected.forEach { uri ->
            assertFalse(adapter.selectLogExportDirectory(uri))
            assertEquals("Download/custom", adapter.preferences.value.logExportLocation)
        }
    }

    private companion object {
        const val LOG_STORE: String = "ghostlock.log"
        const val ATTACK_STORE: String = "ghostlock.attack"
        const val BOOT_STORE: String = "ghostlock.boot"
        const val KEY_AUTO_EXPORT: String = "auto_export"
    }
}
