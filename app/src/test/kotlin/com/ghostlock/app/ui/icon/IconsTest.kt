package com.ghostlock.app.ui.icon

import com.ghostlock.app.ui.UiMode
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

/** Verifies the P-UI-ICON-001 pair, resolver, fallback, and mirrored-icon contracts. */
class IconsTest {
    /** Both renderers resolve their explicitly paired security icon. */
    @Test
    fun resolverSelectsTheRequestedRenderer() {
        val pair = Icons.Filled.Security

        assertSame(pair.material, pair.resolve(UiMode.Material))
        assertSame(pair.miuix, pair.resolve(UiMode.Miuix))
        assertFalse(pair.materialFallback)
        assertFalse(pair.miuixFallback)
    }

    /** A missing Miuix counterpart remains visible through the catalog fallback. */
    @Test
    fun missingCounterpartUsesOnlyThatSidesFallback() {
        val pair = Icons.Filled.Terminal

        assertFalse(pair.materialFallback)
        assertTrue(pair.miuixFallback)
        assertSame(pair.material, pair.resolve(UiMode.Material))
        assertSame(pair.miuix, pair.resolve(UiMode.Miuix))
    }

    /** Layout-direction-aware Material icons keep their explicit Miuix counterpart. */
    @Test
    fun autoMirroredSemanticKeepsBothSides() {
        val pair = Icons.AutoMirrored.Filled.ArrowBack

        assertFalse(pair.materialFallback)
        assertFalse(pair.miuixFallback)
        assertSame(pair.material, pair.resolve(UiMode.Material))
        assertSame(pair.miuix, pair.resolve(UiMode.Miuix))
    }
}
