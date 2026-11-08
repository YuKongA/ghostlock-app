package com.ghostlock.app.data.settings

import androidx.datastore.core.CorruptionException
import androidx.datastore.core.Serializer
import java.io.InputStream
import java.io.OutputStream
import kotlinx.serialization.KSerializer
import kotlinx.serialization.SerializationException
import kotlinx.serialization.json.Json

/**
 * Strict typed DataStore serializer for one complete immutable domain aggregate.
 *
 * @param serializer Generated kotlinx serializer for the aggregate.
 * @param defaultValue Value used only when DataStore has no file yet.
 * @param validate Domain validation applied after read and before write.
 * @note Unknown fields and malformed enum values fail as corruption; this class has no migration logic.
 */
class KotlinxDataStoreSerializer<T>(
    private val serializer: KSerializer<T>,
    override val defaultValue: T,
    private val validate: (T) -> Unit = {},
) : Serializer<T> {
    /** Decodes and validates the complete aggregate or reports corrupt persisted bytes. */
    override suspend fun readFrom(input: InputStream): T = try {
        JsonFormat.decodeFromString(serializer, input.readBytes().decodeToString()).also(validate)
    } catch (problem: CorruptionException) {
        throw problem
    } catch (problem: SerializationException) {
        throw CorruptionException("typed DataStore JSON is malformed", problem)
    } catch (problem: IllegalArgumentException) {
        throw CorruptionException("typed DataStore aggregate violates its domain contract", problem)
    }

    /** Validates and writes one complete aggregate without a wrapper or compatibility envelope. */
    override suspend fun writeTo(t: T, output: OutputStream) {
        validate(t)
        output.write(JsonFormat.encodeToString(serializer, t).encodeToByteArray())
    }

    private companion object {
        val JsonFormat = Json {
            encodeDefaults = true
            ignoreUnknownKeys = false
            explicitNulls = true
            isLenient = false
            allowSpecialFloatingPointValues = false
        }
    }
}
