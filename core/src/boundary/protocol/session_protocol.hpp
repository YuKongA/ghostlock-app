#pragma once
/** @file boundary/protocol/session_protocol.hpp
 *  The boundary wire: one frame format, one op table, one key table.
 *
 *  Why a wire at all: the JNI boundary carries values the execution layer cannot derive (profile
 *  values, session facts) and answers with typed results. Spelling that as a long argument list
 *  would put the field order in two places; a framed, self-describing buffer keeps it in one --
 *  this file -- and lets both ends be checked against the same table.
 *
 *  Layout (all integers little-endian, fixed width):
 *
 *    header (16B):  magic "GLKJ"(4) | version u16 | op u16 | status i32 | entries u32
 *    entry:         key u16 | kind u8 (0 = u64, 1 = bytes) | reserved u8 | length u32 | payload
 *
 *  Rules the codec enforces (each with its own named status):
 *   - the magic and the version are checked before anything else;
 *   - the declared entry count must match the bytes that follow exactly;
 *   - a key outside the table is refused, and a key given twice is refused;
 *   - a u64 entry whose length is not eight is refused by width.
 *
 *  Rules the codec deliberately does NOT enforce: which keys an op requires. That belongs to the
 *  op's own handler, so that "missing" is reported by the layer that knows what it needs.
 *
 *  This unit is JNI-free on purpose: it compiles and is tested on the host, and the JNI entry
 *  point is a thin adapter over it (src/jni/session_api.cpp).
 */
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ghostlock::jni {
    /** The one version this build reads and writes. */
    inline constexpr std::uint16_t kFrameVersion = 1U;
    /** The four bytes that say a buffer is one of ours: "GLKJ" little-endian. */
    inline constexpr std::uint32_t kFrameMagic = 0x4A4B4C47U;
    /** Header bytes before the first entry. */
    inline constexpr std::size_t kFrameHeaderSize = 16U;
    /** How many entries one frame may carry. Larger frames are refused, not truncated. */
    inline constexpr std::size_t kFrameMaxEntries = 64U;
    /** How many bytes one frame may carry. */
    inline constexpr std::size_t kFrameMaxBytes = 64U * 1024U;

    /** What a request asks for. One op is one native transaction; the numbering is the table. */
    enum class FrameOp : std::uint16_t {
        Snapshot = 0,      /**< state, capability descriptor and the session's facts. */
        Initialize = 1,    /**< bring the backend up (route / doors / credentials). */
        Prepare = 2,       /**< 43499: the heap page, the words region and the payload page. */
        Discover = 3,      /**< 43499: the search that finds the target object. */
        SpawnVictim = 4,   /**< 43499: fork the victim and read back its task address. */
        VictimProbe = 5,   /**< 43499: ask the victim what it is (uid, comm). */
        WriteWord = 6,     /**< 43499: one whole word through the window. THE attack op. */
        ReadWord = 7,      /**< 43499: one word back, when the route can read. */
        ResolveTarget = 8, /**< 43284: bind one door to one path, answer a token. */
        WriteBlock = 9,    /**< 43284: one sixteen-byte block through the page cache. THE op. */
        ReadBlock = 10,    /**< 43284: one block back. */
        Trigger = 11,      /**< 43284: the double fork that arms the hooked path. */
        WaitTerminal = 12, /**< 43284: wait for the run's own end state. */
        EmbeddedImage = 13,/**< 43284: the bytes this build carries (helper, landing payload). */
        HookWords = 14,    /**< 43284: the branch word, the displaced word and the payload. */
        Count = 15,        /**< not an op: the first value outside the table. */
    };

    /** How a call ended. Zero is success; every refusal has its own code. */
    enum class FrameStatus : std::int32_t {
        Ok = 0,
        Malformed = -1,      /**< truncated buffer, or the entry count does not match. */
        VersionMismatch = -2,/**< the version field is not kFrameVersion. */
        OpUnknown = -3,      /**< the op is outside the table. */
        KeyUnknown = -4,     /**< an entry names a key this build does not know. */
        KeyDuplicate = -5,   /**< the same key appears twice. */
        KeyMissing = -6,     /**< the handler needs a key the request does not carry. */
        KeyWidth = -7,       /**< a u64 entry whose length is not eight. */
        NotReady = -8,       /**< the session is not up (or not up yet) for this op. */
        Closed = -9,         /**< the session was shut down; nothing is touched. */
        Busy = -10,          /**< another call is inside the session. */
        Unsupported = -11,   /**< this backend does not have the capability. */
        OutOfRange = -12,    /**< the address or the offset is outside what may be touched. */
        BadWidth = -13,      /**< the width is not one this backend can do. */
        Failed = -14,        /**< attempted and failed downstream; the refusal names it. */
        Indeterminate = -15, /**< the call did not come back in a state a caller may trust. */
        StaleTarget = -16,   /**< the token belongs to another session or generation. */
        TooBig = -17,        /**< the frame is larger than kFrameMaxBytes. */
    };

    /** The one spelling of a status: the manifest and the logs both read it from here. */
    [[nodiscard]] constexpr const char *frame_status_name(FrameStatus status) noexcept {
        switch (status) {
            case FrameStatus::Ok: return "ok";
            case FrameStatus::Malformed: return "frame-malformed";
            case FrameStatus::VersionMismatch: return "frame-version";
            case FrameStatus::OpUnknown: return "op-unknown";
            case FrameStatus::KeyUnknown: return "key-unknown";
            case FrameStatus::KeyDuplicate: return "key-duplicate";
            case FrameStatus::KeyMissing: return "key-missing";
            case FrameStatus::KeyWidth: return "key-width";
            case FrameStatus::NotReady: return "not-ready";
            case FrameStatus::Closed: return "closed";
            case FrameStatus::Busy: return "busy";
            case FrameStatus::Unsupported: return "unsupported";
            case FrameStatus::OutOfRange: return "out-of-range";
            case FrameStatus::BadWidth: return "bad-width";
            case FrameStatus::Failed: return "call-failed";
            case FrameStatus::Indeterminate: return "indeterminate";
            case FrameStatus::StaleTarget: return "stale-target";
            case FrameStatus::TooBig: return "frame-too-big";
        }
        return "unknown";
    }

    /** The one spelling of an op name. */
    [[nodiscard]] constexpr const char *frame_op_name(FrameOp op) noexcept {
        switch (op) {
            case FrameOp::Snapshot: return "snapshot";
            case FrameOp::Initialize: return "initialize";
            case FrameOp::Prepare: return "prepare";
            case FrameOp::Discover: return "discover";
            case FrameOp::SpawnVictim: return "spawn_victim";
            case FrameOp::VictimProbe: return "victim_probe";
            case FrameOp::WriteWord: return "write_word";
            case FrameOp::ReadWord: return "read_word";
            case FrameOp::ResolveTarget: return "resolve_target";
            case FrameOp::WriteBlock: return "write_block";
            case FrameOp::ReadBlock: return "read_block";
            case FrameOp::Trigger: return "trigger";
            case FrameOp::WaitTerminal: return "wait_terminal";
            case FrameOp::EmbeddedImage: return "embedded_image";
            case FrameOp::HookWords: return "hook_words";
            case FrameOp::Count: break;
        }
        return "unknown";
    }

    /** What an entry carries. */
    enum class FrameKind : std::uint8_t {
        U64 = 0,   /**< one little-endian 64-bit value, length exactly eight. */
        Bytes = 1, /**< an opaque buffer: a path, an image, or a secret frame. */
    };

    /** The key table. Values are stable: a key is never renumbered, only added. */
    enum class FrameKey : std::uint16_t {
        Backend = 1,     /**< u64: which backend the session is (BackendId). */
        Route = 2,       /**< u64: which route initialize brings up. */
        Phase = 3,       /**< u64: the session's phase, as the descriptor reports it. */
        Generation = 4,  /**< u64: the session generation a token belongs to. */
        Address = 5,     /**< u64: a kernel virtual address (WriteWord / ReadWord). */
        Value = 6,       /**< u64: the value a write carries, or the value a read answered. */
        Form = 7,        /**< u64: WriteForm (clear-preserving / clear-leaf / value-preserving). */
        Offset = 8,      /**< u64: where inside the target (file offset, route offset). */
        Length = 9,      /**< u64: how many bytes an op transfers. */
        Token = 10,      /**< u64: a resolved target's token. */
        Attempts = 11,   /**< u64: how many attempts the caller allows. */
        SettleUs = 12,   /**< u64: how long to wait between attempts, microseconds. */
        DelayMs = 13,    /**< u64: the trigger's delay, milliseconds. */
        PollMs = 14,     /**< u64: the wait's poll interval, milliseconds. */
        Mode = 15,       /**< u64: an op's own mode word (probe kind, wait budget...). */
        Flags = 16,      /**< u64: bit field an op defines for itself. */
        Kind = 17,       /**< u64: which variant of an op is asked for. */
        Index = 18,      /**< u64: a position (image block, word index). */
        Width = 19,      /**< u64: the transfer width an op used, in bytes. */
        Count = 20,      /**< u64: how many entries an answer carries. */
        Bytes = 32,      /**< bytes: an opaque buffer. */
        Path = 33,       /**< bytes: a file path, not NUL-terminated. */
        Text = 34,       /**< bytes: a name (refusal, role, module name). */
        Secret = 35,     /**< bytes: the credentials frame; cleared by both ends after use. */
        Image = 36,      /**< bytes: an image this build carries. */
        CountKey = 37,   /**< not a key: the first value outside the table. */
        /* ---- 运行期事实（Prepare/Discover/SpawnVictim 发布，Kotlin 读） ---- */
        PageBase = 40,   /**< u64: the prepared kernel page. */
        Word0 = 41,      /**< u64: the first futex word the window parks on. */
        Word1 = 42,      /**< u64: the second futex word, the one the arm aims at. */
        PayloadPage = 43,/**< u64: the page the fake objects were written into. */
        PayloadBase = 44,/**< u64: where the payload region starts. */
        AliasDelta = 45, /**< u64: the direct-map alias correction the write needs. */
        Found = 46,      /**< u64: the address the search accepted. */
        VictimTask = 47, /**< u64: the victim task the write aims at. */
        VictimPid = 48,  /**< u64: the victim process. */
        /* ---- 43284（页缓存链）的事实 ---- */
        Sequence = 50,     /**< u64: the SA sequence the last block write left behind. */
        BranchWord = 51,   /**< u64: the word the sentry hook plants at the site. */
        DisplacedWord = 52,/**< u64: the word it displaces (the guard the caller checks). */
        SiteOffset = 53,   /**< u64: where in the hooked image the site is. */
        LandingOffset = 54,/**< u64: where the payload goes. */
    };

    /** The one spelling of a key name; the manifest exports exactly this table. */
    [[nodiscard]] constexpr const char *frame_key_name(FrameKey key) noexcept {
        switch (key) {
            case FrameKey::Backend: return "backend";
            case FrameKey::Route: return "route";
            case FrameKey::Phase: return "phase";
            case FrameKey::Generation: return "generation";
            case FrameKey::Address: return "address";
            case FrameKey::Value: return "value";
            case FrameKey::Form: return "form";
            case FrameKey::Offset: return "offset";
            case FrameKey::Length: return "length";
            case FrameKey::Token: return "token";
            case FrameKey::Attempts: return "attempts";
            case FrameKey::SettleUs: return "settle_us";
            case FrameKey::DelayMs: return "delay_ms";
            case FrameKey::PollMs: return "poll_ms";
            case FrameKey::Mode: return "mode";
            case FrameKey::Flags: return "flags";
            case FrameKey::Kind: return "kind";
            case FrameKey::Index: return "index";
            case FrameKey::Width: return "width";
            case FrameKey::Count: return "count";
            case FrameKey::Bytes: return "bytes";
            case FrameKey::Path: return "path";
            case FrameKey::Text: return "text";
            case FrameKey::Secret: return "secret";
            case FrameKey::Image: return "image";
            case FrameKey::CountKey: break;
            case FrameKey::PageBase: return "page_base";
            case FrameKey::Word0: return "word0";
            case FrameKey::Word1: return "word1";
            case FrameKey::PayloadPage: return "payload_page";
            case FrameKey::PayloadBase: return "payload_base";
            case FrameKey::AliasDelta: return "alias_delta";
            case FrameKey::Found: return "found";
            case FrameKey::VictimTask: return "victim_task";
            case FrameKey::VictimPid: return "victim_pid";
            case FrameKey::Sequence: return "sequence";
            case FrameKey::BranchWord: return "branch_word";
            case FrameKey::DisplacedWord: return "displaced_word";
            case FrameKey::SiteOffset: return "site_offset";
            case FrameKey::LandingOffset: return "landing_offset";
        }
        return "unknown";
    }

    /** @return the op a number denotes, or Count when the number is outside the table. */
    [[nodiscard]] constexpr FrameOp frame_op_of(std::uint16_t raw) noexcept {
        return raw < static_cast<std::uint16_t>(FrameOp::Count) ? static_cast<FrameOp>(raw)
                                                                : FrameOp::Count;
    }

    /** @return the key a number denotes, or CountKey when the number is outside the table. */
    [[nodiscard]] FrameKey frame_key_of(std::uint16_t raw) noexcept;

    /** Zero a buffer that held a secret. Kept here so both ends call the same thing. */
    void zero_bytes(std::span<std::byte> buffer) noexcept;

    /**
     *  A frame under construction.
     *
     *  The buffer is the caller's; the writer never allocates and never grows it. begin() writes
     *  the header with a zero entry count, put_* appends entries, finish() writes the count and
     *  the status. A write that does not fit is refused by return value and leaves the frame
     *  unusable on purpose: a half-written frame must not be sent.
     */
    class FrameWriter final {
      public:
        FrameWriter() noexcept = default;
        explicit FrameWriter(std::span<std::byte> buffer) noexcept : buffer_(buffer) {}

        /** Start a request frame for @p op. */
        [[nodiscard]] bool begin(FrameOp op) noexcept;
        /** Append a u64 entry. */
        [[nodiscard]] bool put_u64(FrameKey key, std::uint64_t value) noexcept;
        /** Append a bytes entry. */
        [[nodiscard]] bool put_bytes(FrameKey key, std::span<const std::byte> payload) noexcept;
        /** Close the frame with its entry count and @p status. */
        [[nodiscard]] bool finish(FrameStatus status) noexcept;

        [[nodiscard]] std::size_t size() const noexcept { return size_; }
        [[nodiscard]] std::span<const std::byte> view() const noexcept {
            return std::span<const std::byte>(buffer_.data(), size_);
        }
        [[nodiscard]] bool usable() const noexcept { return usable_; }

      private:
        std::span<std::byte> buffer_{};
        std::size_t size_ = 0U;
        std::uint32_t entries_ = 0U;
        bool usable_ = false;
    };

    /**
     *  A frame being read.
     *
     *  open() validates the header and every entry in one pass and records where each one is;
     *  lookups then answer from that table, so a malformed frame is refused once, by name, and
     *  never half-read. The reader borrows the caller's buffer: it is valid only while that
     *  buffer is.
     */
    class FrameReader final {
      public:
        FrameReader() noexcept = default;

        /** Validate @p frame and record its entries. @param op receives the op it asks for. */
        [[nodiscard]] FrameStatus open(std::span<const std::byte> frame, FrameOp &op) noexcept;
        /** Find a u64 entry. @return false when the key is absent. */
        [[nodiscard]] bool find_u64(FrameKey key, std::uint64_t &out) const noexcept;
        /** Find a bytes entry. @return false when the key is absent. */
        [[nodiscard]] bool find_bytes(FrameKey key, std::span<const std::byte> &out) const noexcept;
        /** @return true when the key is present at all (any kind). */
        [[nodiscard]] bool has(FrameKey key) const noexcept;
        [[nodiscard]] std::uint32_t entry_count() const noexcept { return entries_; }

      private:
        struct Entry final {
            FrameKey key = FrameKey::CountKey;
            FrameKind kind = FrameKind::U64;
            std::size_t payload = 0U;
            std::size_t length = 0U;
        };
        std::span<const std::byte> frame_{};
        Entry table_[kFrameMaxEntries]{};
        std::uint32_t entries_ = 0U;
    };
}  // namespace ghostlock::jni
