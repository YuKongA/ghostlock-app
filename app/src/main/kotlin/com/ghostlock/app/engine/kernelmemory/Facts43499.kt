package com.ghostlock.app.engine.kernelmemory

/**
 * native snapshot 的领域视图。
 *
 * 字段名来自 native 导出的 [JniBackend43499Port] snapshot 名字表；这里不手抄下标。
 * 三代 generation 在 K1 的 native 快照里尚未发布：缺少时 [generationsAvailable] = false，
 * 引擎在触 native 前拒绝，不本地计数。
 */
data class Facts43499(
    val phase: Long,
    val flags: Long,
    val width: Long,
    val pageBase: Long,
    val word0: Long,
    val word1: Long,
    val payloadPage: Long,
    val payloadBase: Long,
    val aliasDelta: Long,
    val found: Long,
    val mmStruct: Long,
    val victimTask: Long,
    val victimPid: Long,
    val prepareGeneration: Long,
    val discoveryGeneration: Long,
    val victimGeneration: Long,
    val generationsAvailable: Boolean,
) {
    val prepared: Boolean get() = (flags and PREPARED_BIT) != 0L
    val discovered: Boolean get() = (flags and DISCOVERED_BIT) != 0L
    val victimSpawned: Boolean get() = (flags and VICTIM_BIT) != 0L

    companion object {
        /** native 的 kFactPrepared / kFactDiscovered / kFactVictim。 */
        const val PREPARED_BIT = 1L shl 16
        const val DISCOVERED_BIT = 1L shl 17
        const val VICTIM_BIT = 1L shl 18

        /** 必须存在的快照字段；缺一即解码失败。 */
        val REQUIRED_FIELDS = listOf(
            "phase", "flags", "width", "page_base", "word0", "word1", "payload_page",
            "payload_base", "alias_delta", "found", "mm_struct", "victim_task", "victim_pid",
        )

        /** 三代字段；全都存在才算发布。 */
        val GENERATION_FIELDS = listOf(
            "prepare_generation", "discovery_generation", "victim_generation",
        )

        /**
         * 按 native 名字表解码。
         *
         * @throws IllegalArgumentException 名字/值长度不一致、必需字段缺失或重复。
         */
        fun decode(names: Array<String>, values: LongArray): Facts43499 {
            require(names.size == values.size) {
                "snapshot names=" + names.size + " values=" + values.size
            }
            require(names.distinct().size == names.size) { "snapshot field names must be unique" }
            val at = names.withIndex().associate { (index, name) -> name to index }
            for (name in REQUIRED_FIELDS) {
                require(at.containsKey(name)) { "snapshot missing field: " + name }
            }
            fun value(name: String): Long = at[name]?.let { values[it] } ?: 0L
            val generations = GENERATION_FIELDS.all { at.containsKey(it) }
            return Facts43499(
                phase = value("phase"),
                flags = value("flags"),
                width = value("width"),
                pageBase = value("page_base"),
                word0 = value("word0"),
                word1 = value("word1"),
                payloadPage = value("payload_page"),
                payloadBase = value("payload_base"),
                aliasDelta = value("alias_delta"),
                found = value("found"),
                mmStruct = value("mm_struct"),
                victimTask = value("victim_task"),
                victimPid = value("victim_pid"),
                prepareGeneration = value("prepare_generation"),
                discoveryGeneration = value("discovery_generation"),
                victimGeneration = value("victim_generation"),
                generationsAvailable = generations,
            )
        }
    }
}
