package com.ghostlock.app.data.component

/**
 * task-6: one native-exported component vocabulary row.
 *
 * `kind` is the vocabulary name (backend, frontend, stepset today; the route
 * rows join the same table), [wire] is the numeric id native stores in the
 * document slots and [available] mirrors the native availability predicate
 * (available = usable; 0 = declared but rejected before the attack).
 */
data class VocabularyEntry(
    val kind: String,
    val token: String,
    val wire: Int,
    val available: Boolean,
    val doc: String,
)

/**
 * Kotlin half of the component-vocabulary agreement (S4 task-6): the native
 * export `vocabulary-manifest.tsv` is parsed here at runtime, and the App-side
 * enums are asserted against it, so a vocabulary added or retyped natively
 * cannot silently drift.
 *
 * The catalog is deliberately KIND-AGNOSTIC: it groups by the `kind` column and
 * answers lookups per kind, so the route rows (when they land) need no change
 * here. The column set is `kind<TAB>token<TAB>wire<TAB>available<TAB>doc`;
 * parsing is fail-closed (column count, empty fields, `available` other than
 * 0/1, duplicate token or wire inside one kind, empty file) and every failure
 * carries the offending line.
 *
 * The Kotlin enums stay the typed handle; the manifest stays the authority for
 * membership, wire ids and availability.
 */
object VocabularyCatalog {
    /** The native-exported manifest, on the runtime classpath. */
    const val RESOURCE: String = "vocabulary-manifest.tsv"

    private const val COLUMNS = 5
    private val KIND = Regex("[a-z][a-z0-9_]*")

    /** Every row, manifest order. */
    val entries: List<VocabularyEntry> by lazy {
        val stream = VocabularyCatalog::class.java.classLoader
            ?.getResourceAsStream(RESOURCE)
            ?: throw IllegalArgumentException("missing vocabulary manifest resource: " + RESOURCE)
        parse(stream.bufferedReader().use { it.readText() })
    }

    /** Rows grouped by kind, manifest order inside each group. */
    val byKind: Map<String, List<VocabularyEntry>> by lazy { entries.groupBy { it.kind } }

    /** Every row of [kind], manifest order (empty for an unknown kind). */
    fun of(kind: String): List<VocabularyEntry> = byKind[kind].orEmpty()

    /** EXACT token lookup inside [kind] (the vocabulary contract is exact). */
    fun resolve(kind: String, token: String?): VocabularyEntry? {
        if (token == null) return null
        return of(kind).firstOrNull { it.token == token }
    }

    /** EXACT wire lookup inside [kind]. */
    fun resolveWire(kind: String, wire: Int): VocabularyEntry? =
        of(kind).firstOrNull { it.wire == wire }

    /**
     * Parses the manifest text. Shared by the runtime loader and the agreement
     * tests, which parse the committed fixture with this same function.
     */
    fun parse(text: String): List<VocabularyEntry> {
        /* Every contract violation is an IllegalArgumentException (fail closed). */
        fun fail(reason: String): Nothing = throw IllegalArgumentException(reason)
        val out = mutableListOf<VocabularyEntry>()
        val tokens = mutableSetOf<Pair<String, String>>()
        val wires = mutableSetOf<Pair<String, Int>>()
        for (raw in text.lineSequence()) {
            val line = raw.trimEnd('\r')
            if (line.isBlank() || line.startsWith("#")) continue
            val parts = line.split('\t')
            require(parts.size == COLUMNS) {
                "vocabulary manifest line needs " + COLUMNS + " tab-separated columns: " + line
            }
            val kind = parts[0]
            val token = parts[1]
            require(KIND.matches(kind)) { "vocabulary manifest has an invalid kind: " + line }
            require(token.isNotBlank()) { "vocabulary manifest has an empty token: " + line }
            val wire = parts[2].toIntOrNull()
                ?: fail("vocabulary manifest wire is not a number: " + line)
            require(wire >= 0) { "vocabulary manifest has a negative wire id: " + line }
            val available = when (parts[3]) {
                "1" -> true
                "0" -> false
                else -> fail("vocabulary manifest available must be 0/1: " + line)
            }
            require(tokens.add(kind to token)) {
                "duplicate vocabulary token in kind " + kind + ": " + token
            }
            require(wires.add(kind to wire)) {
                "duplicate vocabulary wire id in kind " + kind + ": " + wire
            }
            out += VocabularyEntry(
                kind = kind,
                token = token,
                wire = wire,
                available = available,
                doc = parts[4],
            )
        }
        require(out.isNotEmpty()) { "vocabulary manifest is empty" }
        return out
    }
}
