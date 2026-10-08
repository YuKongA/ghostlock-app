package com.ghostlock.app.ui.theme

import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/**
 * The spacing scale of the app, named once and consumed everywhere.
 *
 * Every value is the one that was already in use: this file NAMES the literals,
 * it does not invent new numbers. Per the UI rule (2026-10-06) screens must not
 * hand-tune pixels, so a screen reads LocalSpacing.current.<name> instead of
 * writing a dp literal - and the values live in exactly one place.
 *
 * This is deliberately NOT a MaterialTheme extension: the project has no
 * material3 dependency (Miuix only). It is provided inside MiuixTheme.
 */
data class GhostlockSpacing(
    /** Widest content column; wider windows get symmetric side padding. */
    val contentMaxWidth: Dp,
    /** Padding above the first element of a page. */
    val pageTop: Dp,
    /** Padding below the last element of a page. */
    val pageBottom: Dp,
    /** Never less side padding than this, even on narrow windows. */
    val pageHorizontalMin: Dp,
    /** Vertical rhythm between two cards/groups in a list. */
    val betweenGroups: Dp,
    /** Gap between adjacent buttons of one action row. */
    val buttonGap: Dp,
    /** Space below a whole section / above the bottom inset. */
    val sectionBottom: Dp,
)

/** The values currently in use across the screens (naming, not new numbers). */
val DefaultGhostlockSpacing = GhostlockSpacing(
    contentMaxWidth = 800.dp,
    pageTop = 8.dp,
    pageBottom = 12.dp,
    pageHorizontalMin = 12.dp,
    betweenGroups = 12.dp,
    buttonGap = 12.dp,
    sectionBottom = 24.dp,
)

/** Provided inside MiuixTheme; read as LocalSpacing.current. */
val LocalSpacing = staticCompositionLocalOf { DefaultGhostlockSpacing }
