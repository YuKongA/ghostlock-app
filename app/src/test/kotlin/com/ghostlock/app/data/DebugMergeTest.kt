package com.ghostlock.app.data

import org.junit.Test

class DebugMergeTest {

    private fun mergeRouteObjects(
        base: ValueMap?,
        incoming: Map<*, *>?,
        replaceBranch: Boolean,
    ): ValueMap? {
        if (incoming == null) return base
        val incomingBranch = incoming.keys.filterIsInstance<String>()
            .firstOrNull { it in listOf("tcp_zerocopy", "select_stack", "multicast_waiter") } ?: return base
        val incomingBody = incoming[incomingBranch].asValueMap() ?: valueMapOf()
        val baseBranch = base?.keys?.toList()
            ?.firstOrNull { it in listOf("tcp_zerocopy", "select_stack", "multicast_waiter") }
        return when {
            baseBranch == incomingBranch -> valueMapOf(
                incomingBranch to deepMergeValues(
                    base[incomingBranch].asValueMap() ?: valueMapOf(),
                    incomingBody,
                ),
            )

            !replaceBranch -> base ?: valueMapOf(incomingBranch to incomingBody)
            else -> valueMapOf(incomingBranch to incomingBody)
        }
    }

    private fun mergeSource(
        base: ValueMap,
        incoming: ValueMap?,
        replaceRouteBranch: Boolean,
    ): ValueMap {
        val baseRoute = base["route"].asValueMap()?.copyValue().asValueMap()
        val baseFallbackRoute = base["fallback"].asValueMap()
            ?.get("route").asValueMap()?.copyValue().asValueMap()
        val merged = deepMergeValues(base, incoming)
        if (incoming == null) return merged
        incoming["route"].asValueMap()?.let { route ->
            mergeRouteObjects(baseRoute, route, replaceRouteBranch)?.let { merged["route"] = it }
        }
        incoming["fallback"].asValueMap()?.get("route").asValueMap()?.let { fallbackRoute ->
            val fallback = merged["fallback"].asValueMap() ?: return@let
            mergeRouteObjects(baseFallbackRoute, fallbackRoute, replaceRouteBranch)
                ?.let { fallback["route"] = it }
        }
        return merged
    }

    @Test
    fun debug() {
        val builtin = valueMapOf(
            "fallback" to valueMapOf("to" to "none"),
            "route" to valueMapOf("select_stack" to valueMapOf("waiter_shift" to -2)),
        )
        val imported = valueMapOf(
            "release" to "r",
            "route" to valueMapOf("multicast_waiter" to valueMapOf("waiter_off" to null)),
            "fallback" to valueMapOf(
                "route" to valueMapOf("tcp_zerocopy" to valueMapOf("compact_waiter" to null)),
                "to" to "tcp_zerocopy",
            ),
        )
        LegacyProfileConverter.convertValue(imported)
        val merged = mergeSource(builtin, imported, replaceRouteBranch = true)
        println("merged fallback=${merged["fallback"]}")
        println("merged route=${merged["route"]}")
        val overrides = valueMapOf()
        val finalMerged = mergeSource(merged, overrides, replaceRouteBranch = false)
        println("final fallback=${finalMerged["fallback"]}")
    }
}
