package com.ghostlock.app.data

/**
 * v2 binary profile framing (the pre-GLKv3 transport), read-only.
 *
 * FORMAT + NUMBERS ONLY — this reader never maps an owner/section name onto the
 * v3 shape and never reinterprets a value: it returns the ordered sections with
 * their RAW u64 payload and leaves every mapping, whitelist and presence decision
 * to [LegacyProfileConverter] (the single migration point).
 *
 * Layout, little-endian, exactly as the removed native writer produced it
 * (evidence: \`git show acaa0ac5^:src/core/profile/binary.{h,cpp}\`):
 *
 *   0..3   magic   = 0x0D000721
 *   4..5   version = 2
 *   6..7   terminal id  (1 = root_child, 2 = umh_forward)
 *   8..9   backend id   (1 = cve_2026_43499, 6 = cve_2026_43284)
 *  10..11  middleware  (high byte reserved, low byte = route id)
 *  12..13  release_length
 *  14..15  padding
 *  then    release bytes (length-prefixed, no NUL)
 *  then    sections(2B)
 *            per section: name_len(1B) + name + entries(4B)
 *              per entry: key_len(1B) + key + raw(8B)
 *
 * There is NO string area in v2 (every field was a numeric member), so a v3 \`str\`
 * target has no v2 source and is not implemented here.
 *
 * FAIL-CLOSED: every bound is checked before it is used, and the document must be
 * consumed EXACTLY — a truncated header/section/entry, an unknown terminal or
 * backend, \`middleware > 0xff\`, a route that does not resolve (with the single
 * documented exception: the 43284 backend may carry \`Auto\` = "no route"), an
 * out-of-range release length or trailing bytes all throw.
 */
internal data class WireV2Entry(val key: String, val raw: ULong)

internal data class WireV2Section(val name: String, val entries: List<WireV2Entry>)

internal data class WireV2Document(
    val terminalId: Int,
    val backendId: Int,
    val middleware: Int,
    /** Low byte of [middleware]; 0 (Auto) only for the 43284 backend. */
    val routeId: Int,
    val release: String,
    val sections: List<WireV2Section>,
)

internal object WireV2Reader {
    /** `binary.h` kMagic / kVersion / kHeaderSize. */
    const val MAGIC: ULong = 0x0D000721uL
    const val VERSION: Int = 2
    const val HEADER_SIZE: Int = 16

    /** TerminalKind::RootChild / UmhForward. */
    private val TERMINALS = setOf(1, 2)

    /** BackendKind::Cve2026_43499 / Cve2026_43284. */
    private val BACKENDS = setOf(1, 6)

    /** RouteKind::TcpZerocopy / SelectStack / MulticastWaiter (Auto excluded). */
    private val ROUTES = setOf(1, 2, 3)
    private const val ROUTE_AUTO = 0
    const val BACKEND_CVE_2026_43284 = 6

    fun read(bytes: ByteArray): WireV2Document {
        require(bytes.size >= HEADER_SIZE) { "v2: truncated header (\${bytes.size} < $HEADER_SIZE)" }
        val magic = readLe(bytes, 0, 4)
        /* ULong: format as Long (java.util.Formatter has no unsigned overload). */
        require(magic == MAGIC) { "v2: bad magic 0x%08x".format(magic.toLong()) }
        val version = readLe(bytes, 4, 2).toInt()
        require(version == VERSION) { "v2: unsupported version $version" }
        val terminal = readLe(bytes, 6, 2).toInt()
        require(terminal in TERMINALS) { "v2: unknown terminal id $terminal" }
        val backend = readLe(bytes, 8, 2).toInt()
        require(backend in BACKENDS) { "v2: unknown backend id $backend" }
        val middleware = readLe(bytes, 10, 2).toInt()
        require(middleware <= 0xFF) { "v2: middleware 0x%x exceeds 0xff".format(middleware) }
        val route = middleware and 0xFF
        require(route in ROUTES || (backend == BACKEND_CVE_2026_43284 && route == ROUTE_AUTO)) {
            "v2: route $route does not resolve (only the 43284 backend may carry Auto)"
        }
        val releaseLength = readLe(bytes, 12, 2).toInt()
        var cursor = HEADER_SIZE
        require(releaseLength <= bytes.size - cursor) {
            "v2: release length $releaseLength exceeds the document"
        }
        val release = String(bytes, cursor, releaseLength, Charsets.UTF_8)
        cursor += releaseLength

        require(bytes.size - cursor >= 2) { "v2: truncated section count" }
        val sectionCount = readLe(bytes, cursor, 2).toInt()
        cursor += 2
        val sections = ArrayList<WireV2Section>(sectionCount)
        for (index in 0 until sectionCount) {
            require(bytes.size - cursor >= 1) { "v2: truncated section #$index name length" }
            val nameLength = bytes[cursor].toInt() and 0xFF
            cursor += 1
            require(nameLength > 0) { "v2: section #$index has an empty name" }
            require(nameLength <= bytes.size - cursor) { "v2: truncated section #$index name" }
            val name = String(bytes, cursor, nameLength, Charsets.UTF_8)
            cursor += nameLength
            require(bytes.size - cursor >= 4) { "v2: truncated section '$name' entry count" }
            val entryCount = readLe(bytes, cursor, 4).toLong()
            cursor += 4
            require(entryCount <= Int.MAX_VALUE) { "v2: section '$name' entry count $entryCount is absurd" }
            val entries = ArrayList<WireV2Entry>(entryCount.toInt())
            for (entryIndex in 0 until entryCount.toInt()) {
                require(bytes.size - cursor >= 1) {
                    "v2: truncated entry #$entryIndex key length in '$name'"
                }
                val keyLength = bytes[cursor].toInt() and 0xFF
                cursor += 1
                require(keyLength > 0) { "v2: '$name' entry #$entryIndex has an empty key" }
                require(keyLength <= bytes.size - cursor) {
                    "v2: truncated key of '$name' entry #$entryIndex"
                }
                val key = String(bytes, cursor, keyLength, Charsets.UTF_8)
                cursor += keyLength
                require(bytes.size - cursor >= 8) { "v2: truncated value of '$name.$key'" }
                entries += WireV2Entry(key, readLe(bytes, cursor, 8))
                cursor += 8
            }
            sections += WireV2Section(name, entries)
        }
        require(cursor == bytes.size) {
            "v2: \${bytes.size - cursor} trailing byte(s) after the last section"
        }
        return WireV2Document(
            terminalId = terminal,
            backendId = backend,
            middleware = middleware,
            routeId = route,
            release = release,
            sections = sections,
        )
    }

    /** Little-endian unsigned integer of [width] bytes at [offset]. */
    private fun readLe(bytes: ByteArray, offset: Int, width: Int): ULong {
        var value = 0uL
        for (index in width - 1 downTo 0) {
            value = (value shl 8) or (bytes[offset + index].toULong() and 0xFFuL)
        }
        return value
    }
}
