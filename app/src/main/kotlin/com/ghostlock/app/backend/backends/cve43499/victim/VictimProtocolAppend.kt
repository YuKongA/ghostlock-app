package com.ghostlock.app.backend.backends.cve43499.victim

/**
 * G04 task-78 append-only 行协议 facade（不修改既有 `VictimProtocol` 成员语义）。
 *
 * @note 实现全部转调既有对象新增的 append 函数；仅供 Kotlin 调用方/测试使用。
 */
internal object VictimProtocolAppend {
    fun statusCommand(): String = VictimProtocol.statusCommand()
    fun commCommand(): String = VictimProtocol.commCommand()
    fun commSetCommand(marker: String): String? = VictimProtocol.commSetCommand(marker)
    fun parseStatusLine(line: String): ByteArray? = VictimProtocol.parseStatusLine(line)
    fun parseCommLine(line: String): Pair<Int, ByteArray>? = VictimProtocol.parseCommLine(line)
    fun parseCommSetLine(line: String): Boolean = VictimProtocol.parseCommSetLine(line)
}
