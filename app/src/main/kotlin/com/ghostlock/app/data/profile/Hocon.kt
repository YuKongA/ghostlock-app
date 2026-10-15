package com.ghostlock.app.data.profile

/**
 * 一个够用的 HOCON 读取器（GLK profile 用到的子集）。
 *
 * 支持的就这些，多一条都不猜：
 *  - 对象：key { ... } 与 key = { ... }
 *  - 数组：key = [ v, { ... }, ... ]（元素之间逗号可选，允许换行）
 *  - 值：字符串（含转义）、整数、小数、true/false/null
 *  - 注释：# 与 // 到行尾
 *  - 引入：include "other.conf"（由调用方提供查文件的办法）
 *  - 点路径取值：a.b.c
 *
 * 明确不做：不做占位替换、不做多文件合并优先级、不做单位后缀（1k/1m）——profile 里没有，
 * 出现即**具名报错**，而不是拿一个大概的数字继续跑。理由：这些值最终会变成内核偏移，猜一个数字
 * 比报错危险得多。
 */
class HoconException(message: String) : Exception(message)

/** 一个配置值：对象 / 数组 / 字符串 / 数 / 布尔 / 空。 */
sealed interface HoconValue {
    data class Obj(val entries: Map<String, HoconValue>) : HoconValue

    data class Arr(val items: List<HoconValue>) : HoconValue

    data class Str(val text: String) : HoconValue

    data class Num(val value: Long, val integral: Boolean = true, val real: Double = 0.0) : HoconValue

    data class Bool(val value: Boolean) : HoconValue

    data object Null : HoconValue
}

/** 一个已解析的配置根。 */
class HoconConfig(private val root: Map<String, HoconValue>) {
    /** @return 点路径上的值；不存在则 null。 */
    fun find(path: String): HoconValue? {
        var current: HoconValue = HoconValue.Obj(root)
        for (segment in path.split('.')) {
            val obj = current as? HoconValue.Obj ?: return null
            current = obj.entries[segment] ?: return null
        }
        return current
    }

    fun has(path: String): Boolean = find(path) != null

    fun obj(path: String): HoconValue.Obj? = find(path) as? HoconValue.Obj

    fun arr(path: String): HoconValue.Arr? = find(path) as? HoconValue.Arr

    /** @return 整数；缺失、类型不符或小数都算失败（null），由调用方给具名理由。 */
    fun long(path: String): Long? = when (val value = find(path)) {
        is HoconValue.Num -> if (value.integral) value.value else null
        is HoconValue.Str -> value.text.toLongOrNull()
        else -> null
    }

    fun string(path: String): String? = when (val value = find(path)) {
        is HoconValue.Str -> value.text
        else -> null
    }

    fun bool(path: String): Boolean? = when (val value = find(path)) {
        is HoconValue.Bool -> value.value
        else -> null
    }

    /** 根上的键（顺序保持解析顺序）。 */
    fun rootKeys(): List<String> = root.keys.toList()

    /** 根上的值。 */
    fun rootValue(name: String): HoconValue? = root[name]

    /** 该路径上的对象的所有键（顺序保持解析顺序）。 */
    fun keys(path: String): List<String> = obj(path)?.entries?.keys?.toList().orEmpty()
}

/** 把文本读成配置。 */
object Hocon {
    /**
     * 解析。
     *
     * @param text 配置文本。
     * @param include 引入解析器：给相对路径，回答该文件的文本；null 表示不接受 include。
     * @param depth 当前引入深度（防环）。
     */
    fun parse(
        text: String,
        include: ((String) -> String?)? = null,
        depth: Int = 0,
    ): HoconConfig {
        require(depth <= 8) { "include depth exceeded" }
        val parser = Parser(text, include, depth)
        return HoconConfig(parser.parseRoot())
    }

    private class Parser(
        private val text: String,
        private val include: ((String) -> String?)?,
        private val depth: Int,
    ) {
        private var at = 0

        fun parseRoot(): Map<String, HoconValue> = parseObject(until = null)

        private fun fail(what: String): Nothing {
            val line = text.substring(0, at.coerceAtMost(text.length)).count { it == '\n' } + 1
            throw HoconException(what + " (line " + line + ")")
        }

        private fun skipSpaceAndComments() {
            while (at < text.length) {
                val c = text[at]
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',') {
                    at++
                } else if (c == '#') {
                    while (at < text.length && text[at] != '\n') {
                        at++
                    }
                } else if (c == '/' && at + 1 < text.length && text[at + 1] == '/') {
                    while (at < text.length && text[at] != '\n') {
                        at++
                    }
                } else {
                    return
                }
            }
        }

        private fun parseObject(until: Char?): Map<String, HoconValue> {
            val entries = LinkedHashMap<String, HoconValue>()
            while (true) {
                skipSpaceAndComments()
                if (at >= text.length) {
                    if (until != null) {
                        fail("unterminated block")
                    }
                    return entries
                }
                if (until != null && text[at] == until) {
                    at++
                    return entries
                }
                val key = parseKey()
                if (key == "include") {
                    /* include 是键位语句：把那份文件的顶层键并进当前对象（HOCON 的语义）。 */
                    skipSpaceAndComments()
                    if (at < text.length && (text[at] == '\'' || text[at] == '"')) {
                        val path = parseQuoted(text[at])
                        val resolver = include ?: fail("include is not allowed here")
                        val other = resolver(path) ?: fail("include not found: " + path)
                        val merged = parse(other, resolver, depth + 1)
                        for (name in merged.rootKeys()) {
                            entries[name] = merged.rootValue(name) ?: HoconValue.Null
                        }
                        continue
                    }
                }
                skipSpaceAndComments()
                if (at < text.length && text[at] == '=') {
                    at++
                    skipSpaceAndComments()
                }
                entries[key] = parseValue()
            }
        }

        private fun parseKey(): String {
            if (at < text.length && (text[at] == '"' || text[at] == '\'')) {
                return parseQuoted(text[at])
            }
            val start = at
            while (at < text.length && (text[at].isLetterOrDigit() || text[at] == '_' || text[at] == '-' ||
                        text[at] == '.' || text[at] == '/')
            ) {
                at++
            }
            if (start == at) {
                fail("expected a key")
            }
            return text.substring(start, at)
        }

        private fun parseQuoted(quote: Char): String {
            at++ // 开引号
            val out = StringBuilder()
            while (at < text.length) {
                val c = text[at]
                if (c == quote) {
                    at++
                    return out.toString()
                }
                if (c == '\\' && at + 1 < text.length) {
                    out.append(text[at + 1])
                    at += 2
                } else {
                    out.append(c)
                    at++
                }
            }
            fail("unterminated string")
        }

        private fun parseValue(): HoconValue {
            skipSpaceAndComments()
            if (at >= text.length) {
                fail("expected a value")
            }
            return when (val c = text[at]) {
                '{' -> {
                    at++
                    HoconValue.Obj(parseObject(until = '}'))
                }

                '[' -> {
                    at++
                    HoconValue.Arr(parseArray())
                }

                '"', '\'' -> HoconValue.Str(parseQuoted(c))

                else -> parseScalar()
            }
        }

        private fun parseArray(): List<HoconValue> {
            val items = mutableListOf<HoconValue>()
            while (true) {
                skipSpaceAndComments()
                if (at >= text.length) {
                    fail("unterminated array")
                }
                if (text[at] == ']') {
                    at++
                    return items
                }
                items += parseValue()
            }
        }

        /**
         * 标量：true/false/null/include/整数/小数。
         *
         * include 在这里处理：它读起来像标量（一个词后面跟一个字符串），读完把那份文件的内容并进
         * 当前对象并**返回一个空对象**，于是"并在哪一层"由调用方决定（GLK profile 的 include 出现在
         * 顶层，语义就是把那份文件的顶层键放进来）。
         */
        private fun parseScalar(): HoconValue {
            val start = at
            while (at < text.length && !" \t\r\n,}]".contains(text[at])) {
                at++
            }
            val token = text.substring(start, at)
            if (token.isEmpty()) {
                fail("empty value")
            }
            when (token) {
                "true" -> return HoconValue.Bool(true)
                "false" -> return HoconValue.Bool(false)
                "null" -> return HoconValue.Null
                "include" -> fail("include must appear as a key, not as a value")
            }
            token.toLongOrNull()?.let { return HoconValue.Num(it) }
            token.toDoubleOrNull()?.let { return HoconValue.Num(0L, integral = false, real = it) }
            fail("unrecognised value: " + token)
        }
    }
}
