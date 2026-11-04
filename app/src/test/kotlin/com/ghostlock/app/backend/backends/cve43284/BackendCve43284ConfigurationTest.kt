package com.ghostlock.app.backend.backends.cve43284

import android.content.Context
import com.ghostlock.app.backend.declarations.BackendConfigStore
import com.ghostlock.app.core.finish.FinishConfig
import com.ghostlock.app.core.finish.FinishState
import com.ghostlock.app.data.backend.BackendSessionStore
import com.ghostlock.app.domain.backend.BackendConfigMutationResult
import com.ghostlock.app.domain.backend.BackendConfigurationMutation
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment

/** Locks the one-way migration to canonical FinishConfig persistence for the 43284 toggle. */
@RunWith(RobolectricTestRunner::class)
class BackendCve43284ConfigurationTest {
    private lateinit var context: Context
    private lateinit var store: BackendConfigStore

    /** Resets both former physical stores so every authority case is isolated. */
    @Before
    fun setUp() {
        context = RuntimeEnvironment.getApplication().applicationContext
        context.getSharedPreferences("ghostlock.finish", Context.MODE_PRIVATE)
            .edit().clear().commit()
        context.getSharedPreferences(
            "ghostlock.backend.config." + BackendCve43284.cve,
            Context.MODE_PRIVATE,
        ).edit().clear().commit()
        FinishConfig.update(context, FinishState())
        store = BackendSessionStore.from(context).configStore(context, BackendCve43284)
        store.clear()
    }

    /** A legacy typed true value migrates into FinishConfig and is then removed. */
    @Test
    fun typedTrueMigratesOnceAndClearsDuplicate() {
        store.putBoolean(FIELD, true)
        BackendCve43284.restoreConfig(store)
        assertTrue(FinishConfig.state.kernelAccessAfterRelay)
        assertFalse(store.contains(FIELD))
    }

    /** A legacy typed false value overrides the old FinishConfig value once and is removed. */
    @Test
    fun typedFalseMigratesOnceAndClearsDuplicate() {
        FinishConfig.update(context, FinishConfig.state.copy(kernelAccessAfterRelay = true))
        store.putBoolean(FIELD, false)
        BackendCve43284.restoreConfig(store)
        assertFalse(FinishConfig.state.kernelAccessAfterRelay)
        assertFalse(store.contains(FIELD))
    }

    /** An absent typed key leaves the canonical FinishConfig value unchanged. */
    @Test
    fun absentTypedKeyNeverReversesCanonicalValue() {
        FinishConfig.update(context, FinishConfig.state.copy(kernelAccessAfterRelay = true))
        BackendCve43284.restoreConfig(store)
        assertTrue(FinishConfig.state.kernelAccessAfterRelay)
        assertFalse(store.contains(FIELD))
    }

    /** Normal capability mutation writes only FinishConfig and never recreates the typed key. */
    @Test
    fun normalMutationDoesNotRecreateTypedDuplicate() {
        val result = BackendCve43284.configurationSpec.mutate(
            store,
            BackendConfigurationMutation.SetBoolean(FIELD, true),
        )
        assertEquals(BackendConfigMutationResult.APPLIED, result)
        assertTrue(FinishConfig.state.kernelAccessAfterRelay)
        assertFalse(store.contains(FIELD))
    }

    /** A wrong command subtype is unsupported and leaves canonical state untouched. */
    @Test
    fun wrongMutationSubtypeIsUnsupported() {
        val result = BackendCve43284.configurationSpec.mutate(
            store,
            BackendConfigurationMutation.SelectValue(FIELD, "true"),
        )
        assertEquals(BackendConfigMutationResult.UNSUPPORTED, result)
        assertFalse(FinishConfig.state.kernelAccessAfterRelay)
        assertFalse(store.contains(FIELD))
    }

    private companion object {
        const val FIELD = "kernel_access_after_relay"
    }
}
