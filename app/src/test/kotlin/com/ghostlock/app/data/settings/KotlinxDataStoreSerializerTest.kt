package com.ghostlock.app.data.settings

import androidx.datastore.core.CorruptionException
import com.ghostlock.app.domain.finish.FinishDefinition
import com.ghostlock.app.domain.finish.defaultFinishDefinition
import com.ghostlock.app.domain.payload.PayloadDefinition
import com.ghostlock.app.domain.privilege.AttackExecutorKind
import com.ghostlock.app.domain.settings.AppSelection
import com.ghostlock.app.domain.settings.OperationalPreferences
import com.ghostlock.app.domain.settings.TerminalLogLevel
import com.ghostlock.app.domain.settings.UiPreferences
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import kotlinx.coroutines.test.runTest
import kotlinx.serialization.KSerializer
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Verifies direct complete-data-class persistence without a document wrapper or compatibility version. */
class KotlinxDataStoreSerializerTest {
    /** All five aggregate shapes preserve complete non-default values. */
    @Test
    fun completeAggregatesRoundTrip() = runTest {
        roundTrip(
            UiPreferences.serializer(),
            UiPreferences(),
            UiPreferences(uiMode = "material", themeMode = 6, pageScale = 1.25f),
        )
        roundTrip(
            OperationalPreferences.serializer(),
            OperationalPreferences(),
            OperationalPreferences(
                logLevel = TerminalLogLevel.Debug,
                showLogTimestamp = true,
                autoExportLogs = false,
                warnOnRerun = false,
                bootRetryCount = 4,
            ),
        )
        roundTrip(
            AppSelection.serializer(),
            AppSelection(),
            AppSelection(AttackExecutorKind.SHIZUKU, "DIAGNOSE"),
        )
        roundTrip(PayloadDefinition.serializer(), PayloadDefinition(), PayloadDefinition(safeMode = true))
        roundTrip(FinishDefinition.serializer(), defaultFinishDefinition(), defaultFinishDefinition())
    }

    /** Unknown persisted fields are corruption rather than an implicit forward-compatibility channel. */
    @Test
    fun unknownFieldIsCorruption() = runTest {
        val serializer = KotlinxDataStoreSerializer(UiPreferences.serializer(), UiPreferences())
        val result = runCatching {
            serializer.readFrom(ByteArrayInputStream("{\"unknown\":true}".encodeToByteArray()))
        }
        assertTrue(result.exceptionOrNull() is CorruptionException)
    }

    private suspend fun <T> roundTrip(serializer: KSerializer<T>, default: T, expected: T) {
        val codec = KotlinxDataStoreSerializer(serializer, default)
        val output = ByteArrayOutputStream()
        codec.writeTo(expected, output)
        assertEquals(expected, codec.readFrom(ByteArrayInputStream(output.toByteArray())))
    }
}
