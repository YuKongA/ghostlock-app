package com.ghostlock.app.data

import com.ghostlock.app.data.route.MulticastConfig
import com.ghostlock.app.data.route.MulticastGeometry
import com.ghostlock.app.data.route.RouteKind
import com.ghostlock.app.data.route.SelectConfig

/** Read-only multicast waiter geometry, mirroring native `MulticastWaiterLayout`. */
internal data class MulticastWaiterLayout(
    val waiterOffset: Long,
    val bufferSize: Long,
    val taskOffset: Long,
    val lockOffset: Long,
    val fakeLockOffset: Long,
    val fakeTaskOffset: Long,
    val lockSlotsOffset: Long,
    val lockSlotCount: Long,
    val lockSlotStride: Long,
    val fakeBssImageOffset: Long,
)

/** Read-only select-stack geometry, mirroring native `SelectStackLayout`. */
internal data class SelectStackLayout(val waiterShift: Long, val compactWaiter: Boolean)

/** Read-only TCP-zerocopy geometry, mirroring native `TcpZerocopyLayout`. */
internal data class TcpZerocopyLayout(val compactWaiter: Boolean)

/**
 * Single authority for one fully resolved profile.
 *
 * The semantic identity (route enum, capabilities, layout views) lives here,
 * while [NativeProfileDocument] remains the v2 codec so the verified byte
 * layout stays authoritative. `toBinary()` must stay byte-identical to the
 * previous direct `NativeProfileDocument.toBinary()` output.

 */
internal data class Profile(
    val document: NativeProfileDocument,
    /** Geometry paths violating the profile rules; never serialized. */
    val invalidPaths: Set<String> = emptySet(),
) {
    val release: String get() = document.release

    /** Resolved route; throws only if a route-less document slipped through. */
    val route: RouteKind
        get() = RouteKind.fromWire(document.routeKind)
            ?: error("profile route is unresolved")

    val fallback: RouteKind? get() = RouteKind.fromWire(document.fallbackRoute)
    val kernelMajor: Long get() = document.kernelMajor
    val recommendShizuku: Boolean get() = document.recommendShizuku != 0L
    val taskStruct: TaskStructOffsets get() = document.taskStruct
    val cred: CredTemplate get() = document.cred
    val kernelOffsets: KernelOffsetTable get() = document.kernelOffset
    val multicast: MulticastGeometry
        get() = (document.routeConfig as? MulticastConfig)?.geometry
            ?: MulticastGeometry(0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L)
    val execution: ExecutionTuning get() = document.execution
    val kernelPhysLoad: Long get() = document.kernelPhysLoad
    val compactWaiter: Boolean get() = document.compactWaiter != 0L
    val pselectWaiterShift: Long
        get() = (document.routeConfig as? SelectConfig)?.waiterShift ?: 0L
    val kernelsnitchCollisions: Long get() = document.kernelsnitchCollisions
    val mmStructSz: Long get() = document.mmStructSz

    fun supports(candidate: RouteKind): Boolean = route == candidate

    fun hasCompactWaiter(): Boolean = compactWaiter

    /** mm_struct stride; a missing or zero value uses [fallback]. */
    fun mmStructStride(fallback: Long): Long = mmStructSz.takeIf { it != 0L } ?: fallback

    fun multicastLayout(): MulticastWaiterLayout = MulticastWaiterLayout(
        waiterOffset = multicast.waiterOff,
        bufferSize = multicast.bufferSize,
        taskOffset = multicast.taskOffset,
        lockOffset = multicast.lockOffset,
        fakeLockOffset = multicast.fakeLockOffset,
        fakeTaskOffset = multicast.fakeTaskOffset,
        lockSlotsOffset = multicast.lockSlotsOffset,
        lockSlotCount = multicast.lockSlotCount,
        lockSlotStride = multicast.lockSlotStride,
        fakeBssImageOffset = (document.routeConfig as? MulticastConfig)?.fakeBssImageOffset ?: 0L,
    )

    fun selectStackLayout(): SelectStackLayout =
        SelectStackLayout(waiterShift = pselectWaiterShift, compactWaiter = compactWaiter)

    fun tcpZerocopyLayout(): TcpZerocopyLayout = TcpZerocopyLayout(compactWaiter)

    fun toNativeDocument(): NativeProfileDocument = document

    fun toBinary(): ByteArray = document.toBinary()

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

        /** Reverse: v2 bytes -> authority (UI / debug / tests). */
        fun fromBinary(bytes: ByteArray): Profile? =
            NativeProfileDocument.fromBinary(bytes)?.let { fromNativeDocument(it) }

        /** Forward: resolved values by dotted path -> authority. */
        fun fromValueMap(
            release: String,
            route: RouteKind?,
            fallbackTo: RouteKind?,
            invalidPaths: Set<String> = emptySet(),
            value: (String) -> Long?,
        ): Profile? = fromNativeDocument(
            document = NativeProfileDocument.from(
                release = release,
                route = route?.token,
                fallbackTo = fallbackTo?.token,
                value = value,
            ),
            invalidPaths = invalidPaths,
        )
    }
}
