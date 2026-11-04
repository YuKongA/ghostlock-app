package com.ghostlock.app.ui.component

import androidx.compose.runtime.Composable

/** Renders reusable content only when its caller-owned capability is available. */
@Composable
fun CapabilityGate(
    available: Boolean,
    content: @Composable () -> Unit,
) {
    if (available) {
        content()
    }
}
