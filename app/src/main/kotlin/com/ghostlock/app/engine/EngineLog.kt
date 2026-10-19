package com.ghostlock.app.engine

/**
 * 生产引擎日志格式：
 *  - info：人类可读短句，不带参数；
 *  - warn/error：人类可读句子 + 恰好一个原因参数；
 *  - debug/verbose：参数放在这里，参数之间用制表符分隔。
 *
 * 这里只负责格式化；级别与去处由调用方决定。
 */
object EngineLog {
    fun info(message: String): String {
        require(!message.contains('\t')) { "info must not carry parameters" }
        require(!message.contains('=')) { "info must not carry parameters" }
        return message
    }

    fun warn(message: String, reason: String): String = withReason(message, reason)

    fun error(message: String, reason: String): String = withReason(message, reason)

    fun debug(message: String, vararg params: String): String = withParams(message, params)

    fun verbose(message: String, vararg params: String): String = withParams(message, params)

    private fun withReason(message: String, reason: String): String {
        require(reason.isNotBlank()) { "warn/error need one reason" }
        require(!reason.contains('\t')) { "warn/error accept one reason only" }
        return message + '\t' + reason
    }

    private fun withParams(message: String, params: Array<out String>): String {
        if (params.isEmpty()) return message
        return message + '\t' + params.joinToString("\t")
    }
}
