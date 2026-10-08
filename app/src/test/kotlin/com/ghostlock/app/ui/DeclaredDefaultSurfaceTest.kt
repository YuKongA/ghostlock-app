package com.ghostlock.app.ui

import com.ghostlock.app.data.NativeProfileDocument
import com.ghostlock.app.data.AvailablePriority
import com.ghostlock.app.data.judgingProfileWithDeclaration
import com.ghostlock.app.data.HoconSupport
import com.ghostlock.app.data.ProfileLayout
import com.ghostlock.app.data.ValueMap
import com.ghostlock.app.data.asValueMap
import com.ghostlock.app.data.component.CombinationCatalog
import com.ghostlock.app.data.component.stepNames
import com.ghostlock.app.data.declarationRefusalFor
import com.ghostlock.app.data.copyValue
import com.ghostlock.app.data.declaredCombinations
import com.ghostlock.app.data.getValueAt
import com.ghostlock.app.data.mutableChild
import com.ghostlock.app.data.valueMapOf
import com.ghostlock.app.data.profile.NativeProfileGlkv3Adapter
import com.ghostlock.app.data.profile.ProfileResolver
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Cross-surface guard (batch (c), design 2.9/U17): the DEFAULT execution path of
 * a profile must survive the whole chain - declaration -> UI default -> document
 * -> wire root route - value for value.
 *
 * HISTORY: this is the shape the A301SO complaint needed. The UI used to start on
 * CombinationCatalog.defaultSpec (a catalogue default that can contradict the
 * declaration), so a profile declaring cve_2026_43284 first emitted a
 * cve_2026_43499 document and the wire carried a route the declared default
 * cannot have. Nothing is hard-coded here: the default comes from
 * declaredCombinations (priority order) joined against the catalogue.
 */
class DeclaredDefaultSurfaceTest {

    private val release = "5.15.189-android13-8-00004-g1c3825f8ac0a-ab14110541"

    private fun queueOf(vararg steps: String): List<Any?> =
        steps.map { valueMapOf("step" to it) }

    /**
     * Canonical profile whose DECLARED default is [defaultBackend].
     *
     * The declaration ORDER is the priority authority (design 2.8.A): an explicit
     * priority replaces map order, so the fixture states its intent through
     * `priority` instead of relying on insertion order (which the authority
     * sorts by token and would silently turn into the opposite case).
     */
    private fun profile(defaultBackend: String): ValueMap {
        val routeLess = { p: Int ->
            valueMapOf("priority" to p, "queue" to queueOf("pagecache_write"))
        }
        val routed = { p: Int ->
            valueMapOf(
                "priority" to p,
                "route" to "multicast_waiter",
                "queue" to queueOf("w1", "w2", "w3"),
            )
        }
        val available = if (defaultBackend == "cve_2026_43284") {
            valueMapOf("cve_2026_43284" to routeLess(1), "cve_2026_43499" to routed(2))
        } else {
            valueMapOf("cve_2026_43499" to routed(1), "cve_2026_43284" to routeLess(2))
        }
        return valueMapOf(
            "schema_version" to 3,
            "release" to release,
            "kernel_major" to 5,
            "available" to available,
            "backend" to valueMapOf(
                "cve_2026_43499" to valueMapOf(
                    "abi" to valueMapOf(
                        "task_struct" to valueMapOf("prio" to 124),
                        "offset" to valueMapOf("init_task" to 47529984L),
                        "cred" to valueMapOf("caps_offset" to 0x88),
                    ),
                ),
            ),
        )
    }

    /** The document the emitter builds for the DECLARED default of [canonical]. */
    private fun defaultDocument(canonical: ValueMap): NativeProfileDocument {
        val declared = declaredCombinations(canonical)
        assertNotNull("the fixture must declare at least one path", declared.firstOrNull())
        val spec = declaredDefaultCombination(declared)
        assertNotNull("the declared default must exist in the catalogue", spec)
        /* The emitter injects exactly this into backend.kind (see (a)). */
        val selected = canonical.copyValue().asValueMap() ?: canonical
        selected.mutableChild("backend")["kind"] = spec!!.backend.token
        val route = spec.route?.token
        return NativeProfileDocument.from(
            release = release,
            route = route,
            raw = { path -> selected.getValueAt(path) },
            value = { path -> ProfileResolver.nativeValue(selected, route, path) },
            text = { path -> ProfileResolver.nativeText(selected, path) },
            bool = { path -> ProfileResolver.nativeBool(selected, path) },
        )
    }

    @Test
    fun `the production declaration capture must unwrap the profile root`() {
        /* Regression guard for the class of defect this batch hit twice: the RAW
         * HOCON parse keeps the R3 wrapper `ghostlock { ... }`, so a top-level read of
         * `available` is null - the capture then silently skips every declaration-
         * driven decision while the runtime carrier still looks right. */
        val raw = valueMapOf(
            "ghostlock" to valueMapOf(
                "schema_version" to 3,
                "release" to release,
                "available" to valueMapOf(
                    "cve_2026_43284" to valueMapOf(
                        "queue" to queueOf("pagecache_write"),
                    ),
                ),
            ),
        )
        assertNull(
            "the raw parse keeps the wrapper, so a top-level read must be null",
            raw["available"],
        )
        val unwrapped = HoconSupport.unwrapProfileDocument(raw).asValueMap()
        val declaration = requireNotNull(unwrapped?.get("available").asValueMap()) {
            "the production capture must unwrap before reading available"
        }
        /* The DECLARATION - not the runtime map and not a fallback - must own the
         * derived backend (zero hard-coding: the expected token is the one declared). */
        assertEquals(
            "the derived backend must come from the declaration",
            "cve_2026_43284",
            /* selectedBackend reads the `available` KEY, so pass a document that
             * carries it - the plain contents would miss and return null. */
            AvailablePriority.selectedBackend(valueMapOf("available" to declaration), null),
        )
    }

    @Test
    fun `the declaration must be visible to the must-have judgement`() {
        /* The runtime map carries no `available`, so the 43499-specific must-have
         * rules (design 2.9-1) were silently skipped. Injecting the declaration must
         * CHANGE the judgement - derived, zero hard-coding: the two error sets are
         * compared, not any particular field. */
        val runtime = valueMapOf("release" to release, "kernel_major" to 5)
        val declaration = valueMapOf(
            "cve_2026_43284" to valueMapOf("queue" to queueOf("pagecache_write")),
            "cve_2026_43499" to valueMapOf(
                "route" to "multicast_waiter",
                "queue" to queueOf("w1", "w2", "w3"),
            ),
        )
        val withoutDeclaration = ProfileResolver
            .validateMerged(runtime, null).map { it.fieldPath }.toSet()
        val withDeclaration = ProfileResolver
            .validateMerged(judgingProfileWithDeclaration(runtime, declaration), null)
            .map { it.fieldPath }.toSet()
        assertNotEquals(
            "declaring 43499 must change the must-have judgement",
            withoutDeclaration,
            withDeclaration,
        )
    }

    @Test
    fun `the store capture must unwrap the profile root too`() {
        /* Same hazard class as the builtin capture: the parsed user document may keep
         * the R3 wrapper, so a top-level read of `available` is null and the
         * declaration is silently empty on that path as well. */
        val entry = valueMapOf(
            "ghostlock" to valueMapOf(
                "schema_version" to 3,
                "release" to release,
                "available" to valueMapOf(
                    "cve_2026_43284" to valueMapOf("queue" to queueOf("pagecache_write")),
                ),
            ),
        )
        assertNull("the raw entry keeps the wrapper", entry["available"])
        val declaration = HoconSupport.unwrapProfileDocument(entry)
            .asValueMap()?.get("available").asValueMap()
        assertNotNull("the store capture must unwrap before reading available", declaration)
        assertEquals(
            "the unwrapped declaration must name the declared backend",
            "cve_2026_43284",
            AvailablePriority.selectedBackend(valueMapOf("available" to declaration), null),
        )
    }

    @Test
    fun `a declared path is never refused and an undeclared one is`() {
        /* PRODUCTION SHAPE: normalize first (the runtime form drops `available`),
         * then judge with the CAPTURED declaration - exactly what the emitter is
         * handed. Zero hard-coding: the default comes from declaredCombinations
         * (priority order) joined against the catalogue. */
        val canonical = ProfileLayout.canonicalize(profile("cve_2026_43284"))
        val captured = requireNotNull(canonical["available"].asValueMap()?.copyValue()?.asValueMap())
        val runtime = canonical.copyValue().asValueMap() ?: canonical
        ProfileLayout.applyNormalize(runtime)
        assertNull("the runtime form carries no declaration", runtime["available"])
        val declaration = valueMapOf("available" to captured)
        val declared = declaredCombinations(declaration)
        assertTrue("the fixture must declare something", declared.isNotEmpty())
        val default = requireNotNull(declaredDefaultCombination(declared))
        assertNull(
            "the declared default must never be refused: " + default.token,
            declarationRefusalFor(declaration, default),
        )
        for (path in declared) {
            val spec = declaredDefaultCombination(listOf(path)) ?: continue
            assertNull(
                "every declared path must stay legal: " + path.backend,
                declarationRefusalFor(declaration, spec),
            )
        }
        /* Non-vacuous: a combination this profile does NOT declare is refused.
         * Derived by SET DIFFERENCE against every declared path (zero hard-coding):
         * the judgement key is (backend, route, stepNames) - terminal does NOT
         * participate - so "some other token" is not enough (mcast_rootchild and
         * mcast_shizuku share the key of the declared 43499 multicast path). */
        val declaredKeys = declared.map { Triple(it.backend, it.route, it.steps) }
        val undeclared = CombinationCatalog.specs.firstOrNull { spec ->
            val key = Triple(
                spec.backend.token,
                spec.route?.token,
                runCatching { spec.steps.stepNames() }.getOrNull().orEmpty(),
            )
            key !in declaredKeys
        }
        requireNotNull(undeclared) {
            "the catalogue has no combination outside the declared keys; catalogue=" +
                CombinationCatalog.specs.size + " declaredKeys=" + declaredKeys
        }
        assertTrue(
            "an undeclared combination must be refused, got null for " + undeclared.token,
            declarationRefusalFor(declaration, undeclared) != null,
        )
    }

    @Test
    fun `the declaration survives the runtime projection`() {
        /* The runtime form DROPS `available` (ProfileLayout.buildRuntime), so the
         * declaration must be captured BEFORE normalization - exactly what the
         * controller (resolve) and UserProfileStore.loadEntryWithDeclaration do.
         * Reading it back from the runtime map yields an EMPTY list, which used to
         * make the UI default and the declaration checks inert in production while
         * passing every canonical-input test. */
        val canonical = ProfileLayout.canonicalize(profile("cve_2026_43284"))
        val captured = canonical["available"].asValueMap()?.copyValue()?.asValueMap()
        val runtime = canonical.copyValue().asValueMap() ?: canonical
        ProfileLayout.applyNormalize(runtime)
        assertNull(
            "the runtime projection must not carry available (that is why it is captured)",
            runtime["available"],
        )
        assertTrue("the declaration must be captured before normalization", captured != null)
        /* declaredCombinations takes the canonical map, so wrap the captured
         * sub-map the same way the canonical shape nests it. */
        val declared = declaredCombinations(valueMapOf("available" to requireNotNull(captured)))
        assertEquals(
            "the captured declaration must still name 43284 as the default",
            "cve_2026_43284",
            declared.firstOrNull()?.backend,
        )
        assertEquals("cve_2026_43284", declaredDefaultCombination(declared)?.backend?.token)
    }

    @Test
    fun `the declared default decides the wire root route`() {
        /* 43284 declared first: no route axis, so the wire must carry NO route. */
        val routeLess = defaultDocument(ProfileLayout.canonicalize(profile("cve_2026_43284")))
        assertNull(
            "the declared default has no route axis: the wire must not carry a route",
            NativeProfileGlkv3Adapter.adapt(routeLess).route,
        )
        /* 43499 declared first: the wire route equals the declared one, value for value. */
        val routed = defaultDocument(ProfileLayout.canonicalize(profile("cve_2026_43499")))
        assertEquals(
            "the declared default route must ride the wire verbatim",
            "multicast_waiter",
            NativeProfileGlkv3Adapter.adapt(routed).route,
        )
    }
}
