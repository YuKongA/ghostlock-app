package com.ghostlock.app.data.nativeaccess

/**
 * The GLKJ frame, from the Kotlin side.
 *
 * One authority for the tables: the native side owns the numbers (core/src/jni/session_protocol.hpp)
 * and exports them as build/native/jni-protocol-manifest.tsv; the lists below are typed mirrors, and
 * NativeProtocolTest fails if any name or number disagrees with that export. Nothing else in the app
 * may spell an op, a key or a status.
 *
 * Layout (little-endian):
 *   header (16B): magic "GLKJ" | version u16 | op u16 | status i32 | entries u32
 *   entry:        key u16 | kind u8 (0 = u64, 1 = bytes) | reserved u8 | length u32 | payload
 */
object NativeProtocol {
    /** The four bytes of a frame, as the uint32 the wire carries. */
    const val MAGIC: Int = 0x4A4B4C47

    /** The only version this build speaks. */
    const val VERSION: Int = 1

    /** Bytes before the first entry. */
    const val HEADER_SIZE: Int = 16

    /** How many entries one frame may carry. */
    const val MAX_ENTRIES: Int = 64

    /** How many bytes one frame may carry. */
    const val MAX_BYTES: Int = 64 * 1024

    /** What a request asks for. */
    enum class Op(val code: Int, val token: String) {
        Snapshot(0, "snapshot"),
        Initialize(1, "initialize"),
        Prepare(2, "prepare"),
        Discover(3, "discover"),
        SpawnVictim(4, "spawn_victim"),
        VictimProbe(5, "victim_probe"),
        WriteWord(6, "write_word"),
        ReadWord(7, "read_word"),
        ResolveTarget(8, "resolve_target"),
        WriteBlock(9, "write_block"),
        ReadBlock(10, "read_block"),
        Trigger(11, "trigger"),
        WaitTerminal(12, "wait_terminal"),
        EmbeddedImage(13, "embedded_image"),
        HookWords(14, "hook_words"),
    }

    /** How a call ended. Zero is success; every refusal has its own code. */
    enum class Status(val code: Int, val token: String) {
        Ok(0, "ok"),
        Malformed(-1, "frame-malformed"),
        VersionMismatch(-2, "frame-version"),
        OpUnknown(-3, "op-unknown"),
        KeyUnknown(-4, "key-unknown"),
        KeyDuplicate(-5, "key-duplicate"),
        KeyMissing(-6, "key-missing"),
        KeyWidth(-7, "key-width"),
        NotReady(-8, "not-ready"),
        Closed(-9, "closed"),
        Busy(-10, "busy"),
        Unsupported(-11, "unsupported"),
        OutOfRange(-12, "out-of-range"),
        BadWidth(-13, "bad-width"),
        Failed(-14, "call-failed"),
        Indeterminate(-15, "indeterminate"),
        StaleTarget(-16, "stale-target"),
        TooBig(-17, "frame-too-big"),
    }

    /** The key table. */
    enum class Key(val code: Int, val token: String) {
        Backend(1, "backend"),
        Route(2, "route"),
        Phase(3, "phase"),
        Generation(4, "generation"),
        Address(5, "address"),
        Value(6, "value"),
        Form(7, "form"),
        Offset(8, "offset"),
        Length(9, "length"),
        Token(10, "token"),
        Attempts(11, "attempts"),
        SettleUs(12, "settle_us"),
        DelayMs(13, "delay_ms"),
        PollMs(14, "poll_ms"),
        Mode(15, "mode"),
        Flags(16, "flags"),
        Kind(17, "kind"),
        Index(18, "index"),
        Width(19, "width"),
        Count(20, "count"),
        Bytes(32, "bytes"),
        Path(33, "path"),
        Text(34, "text"),
        Secret(35, "secret"),
        Image(36, "image"),
        PageBase(40, "page_base"),
        Word0(41, "word0"),
        Word1(42, "word1"),
        PayloadPage(43, "payload_page"),
        PayloadBase(44, "payload_base"),
        AliasDelta(45, "alias_delta"),
        Found(46, "found"),
        VictimTask(47, "victim_task"),
        VictimPid(48, "victim_pid"),
        Sequence(50, "sequence"),
        BranchWord(51, "branch_word"),
        DisplacedWord(52, "displaced_word"),
        SiteOffset(53, "site_offset"),
        LandingOffset(54, "landing_offset"),
    }

    /** A frame under construction. */
    class Builder(private val op: Op) {
        private val out = java.io.ByteArrayOutputStream(HEADER_SIZE + 64)
        private var entries = 0

        init {
            putU32(MAGIC)
            putU16(VERSION)
            putU16(op.code)
            putU32(0) // status, written when the frame is closed
            putU32(0) // entries, written when the frame is closed
        }

        /** Append a u64 entry. */
        fun u64(key: Key, value: ULong): Builder {
            putU16(key.code)
            out.write(0)
            out.write(0)
            putU32(8)
            putU64(value)
            entries++
            return this
        }

        /** Append a bytes entry. */
        fun bytes(key: Key, payload: ByteArray): Builder {
            putU16(key.code)
            out.write(1)
            out.write(0)
            putU32(payload.size)
            out.write(payload)
            entries++
            return this
        }

        /**
         *  Close the frame and answer its bytes: the entry count goes into the header here, because
         *  a frame whose header disagrees with its body is refused by both ends (the native reader
         *  checks exactly that). Status stays zero in a request.
         */
        fun build(): ByteArray {
            val bytes = out.toByteArray()
            for (shift in 0..24 step 8) {
                bytes[12 + shift / 8] = ((entries shr shift) and 0xFF).toByte()
            }
            return bytes
        }

        fun u64OrNull(key: Key, value: ULong?): Builder = if (value == null) this else u64(key, value)

        private fun putU16(value: Int) {
            out.write(value and 0xFF)
            out.write((value shr 8) and 0xFF)
        }

        private fun putU32(value: Int) {
            for (shift in 0..24 step 8) {
                out.write((value shr shift) and 0xFF)
            }
        }

        private fun putU64(value: ULong) {
            for (shift in 0..56 step 8) {
                out.write(((value shr shift) and 0xFFuL).toInt())
            }
        }
    }

    /** A decoded reply. */
    class Reply(
        val status: Status,
        val unknownStatus: Int?,
        val values: Map<Key, ULong>,
        val blobs: Map<Key, ByteArray>,
    ) {
        /** The named refusal the native side put in Text, when it put one. */
        val refusal: String?
            get() = blobs[Key.Text]?.toString(Charsets.UTF_8)

        fun u64(key: Key): ULong? = values[key]

        fun blob(key: Key): ByteArray? = blobs[key]
    }

    /** What decoding a reply can go wrong with. */
    class DecodeException(message: String) : Exception(message)

    /** Read one reply frame. @throws DecodeException when the frame is not one of ours. */
    fun parse(frame: ByteArray): Reply {
        if (frame.size < HEADER_SIZE) {
            throw DecodeException("frame shorter than the header (" + frame.size + ")")
        }
        val magic = readU32(frame, 0)
        if (magic != MAGIC) {
            throw DecodeException("bad magic 0x" + magic.toString(16))
        }
        val version = readU16(frame, 4)
        if (version != VERSION) {
            throw DecodeException("version " + version + " is not " + VERSION)
        }
        val statusCode = readI32(frame, 8)
        val entries = readU32(frame, 12)
        if (entries > MAX_ENTRIES) {
            throw DecodeException("entry count " + entries + " is past the limit")
        }
        val values = HashMap<Key, ULong>()
        val blobs = HashMap<Key, ByteArray>()
        var at = HEADER_SIZE
        for (index in 0 until entries) {
            if (at + 8 > frame.size) {
                throw DecodeException("entry " + index + " runs past the frame")
            }
            val keyCode = readU16(frame, at)
            val kind = frame[at + 2].toInt() and 0xFF
            val length = readU32(frame, at + 4)
            if (length < 0 || at + 8 + length > frame.size) {
                throw DecodeException("entry " + index + " declares " + length + " bytes it does not have")
            }
            val payload = frame.copyOfRange(at + 8, at + 8 + length)
            val key = Key.entries.firstOrNull { it.code == keyCode }
                ?: throw DecodeException("unknown key " + keyCode)
            if (kind == 0) {
                if (length != 8) {
                    throw DecodeException("u64 entry " + key.token + " is " + length + " bytes")
                }
                var value = 0uL
                for (shift in 0..7) {
                    value = value or ((payload[shift].toULong() and 0xFFuL) shl (8 * shift))
                }
                values[key] = value
            } else {
                blobs[key] = payload
            }
            at += 8 + length
        }
        if (at != frame.size) {
            throw DecodeException("trailing bytes: " + (frame.size - at))
        }
        val known = Status.entries.firstOrNull { it.code == statusCode }
        return Reply(known ?: Status.Failed, if (known == null) statusCode else null, values, blobs)
    }

    private fun readU16(bytes: ByteArray, at: Int): Int =
        (bytes[at].toInt() and 0xFF) or ((bytes[at + 1].toInt() and 0xFF) shl 8)

    private fun readU32(bytes: ByteArray, at: Int): Int {
        var value = 0
        for (shift in 0..24 step 8) {
            value = value or ((bytes[at + shift / 8].toInt() and 0xFF) shl shift)
        }
        return value
    }

    private fun readI32(bytes: ByteArray, at: Int): Int = readU32(bytes, at)
}
