package com.ghostlock.app.backend.backends

import android.content.Context
import android.net.Uri
import com.ghostlock.app.backend.backends.cve43499glued.BackendCve43499Glued
import com.ghostlock.app.backend.backends.lua.BackendCustom
import com.ghostlock.app.backend.backends.lua.LuaScriptSelectionStore
import com.ghostlock.app.backend.declarations.BackendConfigSchema
import com.ghostlock.app.backend.declarations.BackendConfigStore
import com.ghostlock.app.backend.declarations.BackendConfigViolation
import com.ghostlock.app.domain.backend.BackendConfigurationMutation
import java.io.ByteArrayInputStream
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows.shadowOf

/** Verifies that configured backends publish runtime mirrors only after typed-store commit. */
@RunWith(RobolectricTestRunner::class)
class BackendConfigurationCommitOrderingTest {
    private lateinit var context: Context

    /** Resets both process-local mirrors before each isolated commit-order case. */
    @Before
    fun setUp() {
        context = RuntimeEnvironment.getApplication().applicationContext
        BackendCve43499Glued.clearProfile(
            MemoryConfigStore(context, BackendCve43499Glued.configSchema),
        )
        LuaScriptSelectionStore.select(context, null)
    }

    /** A 43499 store failure leaves the previously validated profile mirror unchanged. */
    @Test
    fun profileCommitFailureDoesNotClearRuntimeMirror() {
        val initialStore = MemoryConfigStore(context, BackendCve43499Glued.configSchema)
        BackendCve43499Glued.selectProfile("builtin:before", initialStore)

        val failure = assertThrows(BackendConfigViolation::class.java) {
            BackendCve43499Glued.configurationSpec.mutate(
                MemoryConfigStore(
                    context = context,
                    schema = BackendCve43499Glued.configSchema,
                    failStringWrites = true,
                ),
                BackendConfigurationMutation.SelectValue(PROFILE_FIELD, null),
            )
        }

        assertEquals("backend config commit failed", failure.message)
        assertEquals("builtin:before", BackendCve43499Glued.profileReference)
        assertEquals(true, BackendCve43499Glued.profileState.value.validated)
    }

    /** A Lua store failure occurs after document read but before runtime selection publication. */
    @Test
    fun luaCommitFailureDoesNotPublishRuntimeMirror() {
        val uri = Uri.parse("content://ghostlock.test/script.lua")
        shadowOf(context.contentResolver).registerInputStream(
            uri,
            ByteArrayInputStream("print('fixture')\n".toByteArray()),
        )

        val failure = assertThrows(BackendConfigViolation::class.java) {
            BackendCustom.configurationSpec.mutate(
                MemoryConfigStore(
                    context = context,
                    schema = BackendCustom.configSchema,
                    failStringWrites = true,
                ),
                BackendConfigurationMutation.ImportDocument(SCRIPT_FIELD, uri.toString()),
            )
        }

        assertEquals("backend config commit failed", failure.message)
        assertNull(LuaScriptSelectionStore.state.value.uri)
        assertNull(LuaScriptSelectionStore.state.value.summary)
    }

    /** Minimal schema-aware store fixture with one controllable synchronous commit failure. */
    private class MemoryConfigStore(
        override val context: Context,
        override val schema: BackendConfigSchema,
        private val failStringWrites: Boolean = false,
    ) : BackendConfigStore {
        private val values = mutableMapOf<String, Any>()

        override val cve: String = "TEST"

        override fun contains(name: String): Boolean = values.containsKey(name)

        override fun getBoolean(name: String): Boolean =
            values[name] as? Boolean ?: schema.key(name)?.defaultValue as Boolean

        override fun getInt(name: String): Int =
            values[name] as? Int ?: schema.key(name)?.defaultValue as Int

        override fun getLong(name: String): Long =
            values[name] as? Long ?: schema.key(name)?.defaultValue as Long

        override fun getString(name: String): String =
            values[name] as? String ?: schema.key(name)?.defaultValue as String

        override fun putBoolean(name: String, value: Boolean) {
            values[name] = value
        }

        override fun putInt(name: String, value: Int) {
            values[name] = value
        }

        override fun putLong(name: String, value: Long) {
            values[name] = value
        }

        override fun putString(name: String, value: String) {
            if (failStringWrites) throw BackendConfigViolation("backend config commit failed")
            values[name] = value
        }

        override fun clear() {
            values.clear()
        }

        override fun fullKey(name: String): String = "$cve:$name"
    }

    private companion object {
        const val PROFILE_FIELD = "profile_reference"
        const val SCRIPT_FIELD = "script_uri"
    }
}
