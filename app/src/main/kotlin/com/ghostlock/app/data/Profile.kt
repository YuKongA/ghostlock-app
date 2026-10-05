package com.ghostlock.app.data

import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.route.MulticastConfig
import com.ghostlock.app.data.route.MulticastGeometry
import com.ghostlock.app.data.route.RouteKind
import com.ghostlock.app.data.route.SelectConfig

/** Read-only multicast waiter geometry, mirroring native `MulticastWaiterLayout`. */
internal data class MulticastWaiterLayout(
    val waiterOffset: Int?,
    val bufferSize: UInt?,
    val taskOffset: UInt?,
    val lockOffset: UInt?,
)

/** Read-only select-stack geometry, mirroring native `SelectStackLayout`. */
internal data class SelectStackLayout(val waiterShift: Int?, val compactWaiter: Boolean)

/**
 * Single authority for one fully resolved profile.
 *
 * The semantic identity (route enum, capabilities, layout views) lives here,
 * while [NativeProfileDocument] remains the logical model that the GLKv3
 * adapter translates to the wire.
 */
internal data class Profile(
    val document: NativeProfileDocument,
    /** Geometry paths violating the profile rules; never serialized. */
    val invalidPaths: Set<String> = emptySet(),
) {
    val release: String get() = document.release

    /** Backend selection carried in the document; defaults to 43499. */
    val backendKind: BackendKind
        get() = BackendKind.fromWire(document.backendKind.toInt()) ?: BackendKind.Default

    /** Resolved route; throws only if a route-less document slipped through. */
    val route: RouteKind
        get() = RouteKind.fromWire(document.routeKind)
            ?: error("profile route is unresolved")

    val fallback: RouteKind? get() = RouteKind.fromWire(document.fallbackRoute)
    val kernelMajor: UInt get() = document.kernelMajor
    val cred: CredTemplate get() = document.cred
    val multicast: MulticastGeometry
        get() = (document.routeConfig as? MulticastConfig)?.geometry
            ?: MulticastGeometry(null, null, null, null)
    val execution: ExecutionTuning get() = document.execution
    val compactWaiter: Boolean get() = (document.compactWaiter?.toInt() ?: 0) != 0
    val pselectWaiterShift: Int?
        get() = (document.routeConfig as? SelectConfig)?.waiterShift
    val mmStructSz: UInt? get() = document.mmStructSz

    fun supports(candidate: RouteKind): Boolean = route == candidate

    fun hasCompactWaiter(): Boolean = compactWaiter

    /** mm_struct stride; a missing or zero value uses [fallback]. */
    fun mmStructStride(fallback: UInt): UInt = mmStructSz?.takeIf { it != 0u } ?: fallback

    fun multicastLayout(): MulticastWaiterLayout = MulticastWaiterLayout(
        waiterOffset = multicast.waiterOff,
        bufferSize = multicast.bufferSize,
        taskOffset = multicast.taskOffset,
        lockOffset = multicast.lockOffset,
    )

    fun selectStackLayout(): SelectStackLayout =
        SelectStackLayout(waiterShift = pselectWaiterShift, compactWaiter = compactWaiter)

    companion object {
        /** Wraps a decoded document, rejecting an unresolved route. */
        fun fromNativeDocument(
            document: NativeProfileDocument,
            invalidPaths: Set<String> = emptySet(),
        ): Profile? = if (RouteKind.fromWire(document.routeKind) == null) {
            null
        } else {
            Profile(document, invalidPaths)
        }

        /** Forward: resolved values by dotted path -> authority. */
        fun fromValueMap(
            release: String,
            route: RouteKind?,
            fallbackTo: RouteKind?,
            invalidPaths: Set<String> = emptySet(),
            text: (String) -> String? = { null },
            bool: (String) -> Boolean? = { null },
            value: (String) -> Long?,
        ): Profile? = fromNativeDocument(
            document = NativeProfileDocument.from(
                release = release,
                route = route?.token,
                fallbackTo = fallbackTo?.token,
                value = value,
                text = text,
                bool = bool,
            ),
            invalidPaths = invalidPaths,
        )
    }
}
