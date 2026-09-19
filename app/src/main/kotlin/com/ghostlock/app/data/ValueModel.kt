package com.ghostlock.app.data

/**
 * The configuration value model: Typesafe Config's unwrapped shape
 * (LinkedHashMap / ArrayList / scalars). Everything the application reads,
 * merges, persists and hands to native goes through these helpers.
 */
internal typealias ValueMap = LinkedHashMap<String, Any?>
internal typealias ValueList = ArrayList<Any?>

internal fun valueMapOf(vararg pairs: Pair<String, Any?>): ValueMap = linkedMapOf(*pairs)

internal fun valueListOf(vararg items: Any?): ValueList = ArrayList(items.toList())

@Suppress("UNCHECKED_CAST")
internal fun Any?.asValueMap(): ValueMap? {
    val map = this as? Map<*, *> ?: return null
    return map as? ValueMap
        ?: ValueMap().apply { map.forEach { (key, value) -> put(key.toString(), value) } }
}

internal fun Any?.asValueList(): ValueList? {
    val list = this as? List<*> ?: return null
    return list as? ValueList ?: ValueList().apply { addAll(list) }
}

internal fun Any?.copyValue(): Any? = when (this) {
    is Map<*, *> -> {
        val copy = ValueMap()
        for ((key, value) in this) copy[key.toString()] = value.copyValue()
        copy
    }

    is List<*> -> {
        val copy = ValueList()
        for (item in this) copy.add(item.copyValue())
        copy
    }

    else -> this
}

internal fun Map<String, Any?>.getValueAt(path: String): Any? {
    var node: Any? = this
    for (segment in path.split('.')) {
        node = when (val current = node) {
            is Map<*, *> -> current[segment]
            is List<*> -> segment.toIntOrNull()?.let(current::getOrNull)
            else -> return null
        } ?: return null
    }
    return node
}

internal fun Map<String, Any?>.getLongAt(path: String): Long? =
    (getValueAt(path) as? Number)?.toLong()

internal fun MutableMap<String, Any?>.setValueAt(path: String, value: Any?) {
    val segments = path.split('.')
    var node: MutableMap<String, Any?> = this
    for (index in 0 until segments.size - 1) {
        node = node.mutableChild(segments[index])
    }
    node[segments.last()] = value
}

internal fun MutableMap<String, Any?>.removeValueAt(path: String) {
    val segments = path.split('.')
    val parents = ArrayList<Pair<MutableMap<String, Any?>, String>>()
    var node: MutableMap<String, Any?> = this
    for (index in 0 until segments.size - 1) {
        val child = node.mutableChildOrNull(segments[index]) ?: return
        parents += node to segments[index]
        node = child
    }
    node.remove(segments.last())
    for ((parent, key) in parents.asReversed()) {
        val child = parent[key] as? Map<*, *> ?: break
        if (child.isEmpty()) parent.remove(key) else break
    }
}

@Suppress("UNCHECKED_CAST")
internal fun MutableMap<String, Any?>.mutableChild(key: String): ValueMap =
    (this[key] as? ValueMap) ?: ValueMap().also { this[key] = it }

@Suppress("UNCHECKED_CAST")
internal fun MutableMap<String, Any?>.mutableChildOrNull(key: String): ValueMap? =
    this[key] as? ValueMap

internal fun deepMergeValues(base: ValueMap, override: Map<String, Any?>?): ValueMap {
    if (override == null) return base
    for ((key, incoming) in override) {
        val current = base[key]
        when {
            incoming is Map<*, *> && current is Map<*, *> ->
                base[key] = deepMergeValues(asMutableMap(current), incoming.asValueMap())

            incoming != null -> base[key] = incoming
        }
    }
    return base
}

internal fun asMutableMap(source: Map<*, *>): ValueMap =
    source as? ValueMap ?: ValueMap().apply { source.forEach { (key, value) -> put(key.toString(), value) } }
