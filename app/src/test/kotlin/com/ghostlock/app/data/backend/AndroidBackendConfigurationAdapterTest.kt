package com.ghostlock.app.data.backend

import android.content.Context
import com.ghostlock.app.R
import com.ghostlock.app.backend.declarations.BackendConfigStore
import com.ghostlock.app.backend.declarations.BackendConfigurationSpec
import com.ghostlock.app.backend.declarations.BackendRow
import com.ghostlock.app.backend.declarations.BackendSpec
import com.ghostlock.app.domain.backend.BackendConfigIcon
import com.ghostlock.app.domain.backend.BackendConfigMutationResult
import com.ghostlock.app.domain.backend.BackendConfiguration
import com.ghostlock.app.domain.backend.BackendConfigurationMutation
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment

/** Characterizes generic BackendSpec configuration delegation without concrete backend branches. */
@RunWith(RobolectricTestRunner::class)
class AndroidBackendConfigurationAdapterTest {
    private lateinit var context: Context

    /** Clears adapter-owned preferences before each isolated capability test. */
    @Before
    fun setUp() {
        context = RuntimeEnvironment.getApplication().applicationContext
        context.getSharedPreferences("ghostlock.backend.session", Context.MODE_PRIVATE)
            .edit().clear().commit()
        context.getSharedPreferences("ghostlock.backend.config.fake", Context.MODE_PRIVATE)
            .edit().clear().commit()
    }

    /** APPLIED delegates once and refreshes exactly once through the same capability. */
    @Test
    fun appliedMutationRefreshesExactlyOnce() {
        val capability = FakeCapability()
        val adapter = adapter(FakeSpec("fake", capability))
        assertEquals(1, capability.snapshotCalls)

        assertEquals(
            BackendConfigMutationResult.APPLIED,
            adapter.setBoolean("fake", FIELD, true),
        )

        assertEquals(1, capability.mutationCalls)
        assertEquals(2, capability.snapshotCalls)
        assertEquals(true, (adapter.states.value.getValue("fake") as BackendConfiguration.Toggle).enabled)
    }

    /** Unknown backend keys never reach another backend and never refresh state. */
    @Test
    fun unknownKeyIsUnsupportedWithoutFallback() {
        val capability = FakeCapability()
        val adapter = adapter(FakeSpec("fake", capability))
        assertEquals(
            BackendConfigMutationResult.UNSUPPORTED,
            adapter.setBoolean("missing", FIELD, true),
        )
        assertEquals(0, capability.mutationCalls)
        assertEquals(1, capability.snapshotCalls)
    }

    /** A capability exception maps to INVALID and does not trigger the refresh path. */
    @Test
    fun capabilityFailureIsInvalidWithoutRefresh() {
        val capability = FakeCapability(throwOnMutation = true)
        val adapter = adapter(FakeSpec("fake", capability))
        assertEquals(
            BackendConfigMutationResult.INVALID,
            adapter.setBoolean("fake", FIELD, true),
        )
        assertEquals(1, capability.mutationCalls)
        assertEquals(1, capability.snapshotCalls)
    }

    /** A backend without interactive capability is projected from rows as Static. */
    @Test
    fun missingCapabilityUsesStaticRows() {
        val adapter = adapter(FakeSpec("static", null))
        val state = adapter.states.value.getValue("static") as BackendConfiguration.Static
        assertEquals("static-value", state.values.single().value)
        assertTrue(state.values.single().label.isNotBlank())
        assertNull(FakeSpec("another", null).configurationSpec)
    }

    /** @return Adapter bound to one isolated captured registry. */
    private fun adapter(spec: BackendSpec): AndroidBackendConfigurationAdapter =
        AndroidBackendConfigurationAdapter(
            context = context,
            registryEntries = listOf(spec),
            sessionStore = BackendSessionStore.from(context),
        )

    /** Minimal outer plugin declaration used by the generic adapter tests. */
    private class FakeSpec(
        override val key: String,
        override val configurationSpec: BackendConfigurationSpec?,
    ) : BackendSpec {
        override val cve: String = key
        override val titleRes: Int = R.string.backend_custom
        override fun rows(): List<BackendRow> =
            listOf(BackendRow(R.string.backend_row_status, "static-value"))
    }

    /** Stateful capability fixture that records projection and mutation calls. */
    private class FakeCapability(
        private val throwOnMutation: Boolean = false,
    ) : BackendConfigurationSpec {
        var snapshotCalls: Int = 0
            private set
        var mutationCalls: Int = 0
            private set
        private var enabled: Boolean = false

        override fun snapshot(store: BackendConfigStore): BackendConfiguration.Toggle {
            snapshotCalls += 1
            return BackendConfiguration.Toggle(
                key = store.cve,
                field = FIELD,
                title = "Toggle",
                enabled = enabled,
                enabledSummary = "On",
                disabledSummary = "Off",
                icon = BackendConfigIcon.NONE,
            )
        }

        override fun mutate(
            store: BackendConfigStore,
            mutation: BackendConfigurationMutation,
        ): BackendConfigMutationResult {
            mutationCalls += 1
            if (throwOnMutation) error("fixture mutation failure")
            if (mutation !is BackendConfigurationMutation.SetBoolean || mutation.field != FIELD) {
                return BackendConfigMutationResult.UNSUPPORTED
            }
            enabled = mutation.value
            return BackendConfigMutationResult.APPLIED
        }
    }

    private companion object {
        const val FIELD = "enabled"
    }
}
