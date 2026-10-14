package com.ghostlock.app.core

/**
 * A capability borrowed from an [AccessSession].
 *
 * The descriptor describes one established session, not every operation a backend might
 * eventually implement. The capability becomes invalid when its session closes.
 */
sealed interface AccessCapability {
    val descriptor: AccessDescriptor
}
