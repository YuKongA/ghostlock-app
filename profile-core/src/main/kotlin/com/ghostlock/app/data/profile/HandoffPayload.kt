package com.ghostlock.app.data.profile

import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.component.FrontendKind
import java.security.MessageDigest

/**
 * A6-4 document segment three: one finishing payload (a plan, a script or a module).
 *
 * The document carries the payload bytes and the digest they are bound to, and the
 * native side re-computes that digest before anything is written or run
 * (src/core/terminal/payload.hpp). HOCON declares intent; this object is what
 * resolve produces (docs/analysis/lua-surface-annex-document.md section B1 row 3).
 *
 * Naming is a rule, not a convention: a name starts with a SELECTABLE terminal
 * token the tree declares and continues with an underscore and a bounded suffix,
 * so a handoff payload_ref always names the payload of a terminal that exists and
 * a bare terminal name (one concept, two shapes) is refused. Keys, kinds, the
 * suffix bound and the name rule are mirrored from the native authority
 * (src/core/profile/payload_keys.hpp; pin src/core/tests/payload-manifest.pin.tsv).
 * The terminal set is READ from [FrontendKind] instead of being written down here,
 * so a terminal added to the catalogue becomes admissible without a second edit
 * (and shizuku, a combination token rather than a terminal, never is).
 *
 * @property name segment-local name; a handoff payload_ref spells this verbatim.
 * @property kind one of [KINDS].
 * @property bytes the payload itself; never empty (absent content is not declared).
 */
class HandoffPayload private constructor(
    val name: String,
    val kind: String,
    val bytes: ByteArray,
) {
    /** Lower-case hex SHA-256 of [bytes], exactly 64 characters. */
    val sha256: String = hexSha256(bytes)

    /** The declared byte count; equals the length of [bytes]. */
    val size: UInt = bytes.size.toUInt()

    /** The dotted segment this payload lives in. */
    val segment: String = SEGMENT_PREFIX + "." + name

    override fun equals(other: Any?): Boolean =
        other is HandoffPayload && name == other.name && kind == other.kind &&
            bytes.contentEquals(other.bytes)

    override fun hashCode(): Int =
        (name.hashCode() * 31 + kind.hashCode()) * 31 + bytes.contentHashCode()

    override fun toString(): String =
        "HandoffPayload(" + name + ", kind=" + kind + ", size=" + size +
            ", sha256=" + sha256 + ")"

    companion object {
        /** The four keys of one payload, in the native order. */
        val KEYS: List<String> = listOf("bin", "sha256", "size", "kind")

        /** The values the kind key may carry (native kPayloadKinds). */
        val KINDS: List<String> = listOf("plan", "script", "module")

        /** The suffix bound (native kPayloadSuffixMax). */
        const val SUFFIX_MAX: Int = 16

        /** The owning backend token, from the App-side enum authority. */
        val OWNER_TOKEN: String = BackendKind.Cve2026_43499.token

        /** backend.<id>.payload: the one segment spelling (native payload_segment). */
        val SEGMENT_PREFIX: String = "backend." + OWNER_TOKEN + ".payload"

        /** The selectable terminals a name may start with (never hardcoded). */
        val TERMINALS: List<String>
            get() = FrontendKind.entries.filter { it.available }.map { it.token }

        /**
         * The declared terminal a name starts with, or null when none does
         * (native terminal_of: a prefix counts only at an underscore boundary).
         *
         * @param name candidate payload name.
         * @return the terminal token, or null.
         */
        fun terminalOf(name: String): String? = TERMINALS.firstOrNull { token ->
            name.startsWith(token) &&
                (name.length == token.length || name[token.length] == '_')
        }

        /**
         * The suffix rule: one to [SUFFIX_MAX] characters of [a-z0-9_].
         *
         * @param suffix the part after the terminal and its underscore.
         * @return true when the suffix is admissible.
         */
        fun suffixIsValid(suffix: String): Boolean =
            suffix.isNotEmpty() && suffix.length <= SUFFIX_MAX &&
                suffix.all { c -> (c in 'a'..'z') || (c in '0'..'9') || c == '_' }

        /**
         * The whole name rule in one place (native payload_name_is_valid):
         * selectable terminal, underscore, admissible suffix; a bare terminal is
         * refused so the family keeps exactly one shape.
         *
         * @param name candidate payload name.
         * @return true when the name is admissible.
         */
        fun nameIsValid(name: String): Boolean {
            val terminal = terminalOf(name) ?: return false
            if (name.length <= terminal.length + 1) return false
            return suffixIsValid(name.substring(terminal.length + 1))
        }

        /**
         * Builds a payload, refusing everything the native reader would refuse.
         *
         * @param name segment-local payload name (see [nameIsValid]).
         * @param kind one of [KINDS].
         * @param bytes payload content; must not be empty.
         * @return the payload, with its digest and size derived from [bytes].
         * @throws IllegalArgumentException when the name, the kind or the content
         *   is inadmissible (fail-closed before anything reaches the wire).
         */
        fun of(name: String, kind: String, bytes: ByteArray): HandoffPayload {
            require(nameIsValid(name)) {
                "inadmissible payload name '" + name + "': expected <terminal>_<suffix> with a " +
                    "suffix of 1.." + SUFFIX_MAX + " characters [a-z0-9_] and a selectable " +
                    "terminal (" + TERMINALS.joinToString("/") + ")"
            }
            require(kind in KINDS) {
                "undeclared payload kind '" + kind + "': expected " + KINDS.joinToString("/")
            }
            require(bytes.isNotEmpty()) {
                "payload '" + name + "' declares no bytes: absent content is not declared " +
                    "(the native check refuses an empty payload by name)"
            }
            return HandoffPayload(name, kind, bytes)
        }

        private fun hexSha256(bytes: ByteArray): String {
            val digest = MessageDigest.getInstance("SHA-256").digest(bytes)
            val out = StringBuilder(digest.size * 2)
            for (byte in digest) {
                val value = byte.toInt() and 0xFF
                out.append(HEX[value ushr 4]).append(HEX[value and 0x0F])
            }
            return out.toString()
        }

        private const val HEX = "0123456789abcdef"
    }
}
