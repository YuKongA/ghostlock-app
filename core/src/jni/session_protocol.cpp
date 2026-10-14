/** @file jni/session_protocol.cpp
 *  The boundary wire's codec: one pass to validate, one table to look up.
 *
 *  Nothing here allocates, touches a file, or knows anything about a backend: the frame format is
 *  the whole subject (see the header for the layout and for which rule each status means).
 */
#include "jni/session_protocol.hpp"

#include <cstring>

namespace ghostlock::jni {
    namespace {
        /** Write one little-endian value into @p at. Callers have already checked room. */
        void store_u16(std::byte *at, std::uint16_t value) noexcept {
            at[0] = static_cast<std::byte>(value & 0xffU);
            at[1] = static_cast<std::byte>((value >> 8U) & 0xffU);
        }
        void store_u32(std::byte *at, std::uint32_t value) noexcept {
            for (std::size_t i = 0U; i < 4U; ++i) {
                at[i] = static_cast<std::byte>((value >> (8U * i)) & 0xffU);
            }
        }
        void store_u64(std::byte *at, std::uint64_t value) noexcept {
            for (std::size_t i = 0U; i < 8U; ++i) {
                at[i] = static_cast<std::byte>((value >> (8U * i)) & 0xffU);
            }
        }
        [[nodiscard]] std::uint16_t load_u16(const std::byte *at) noexcept {
            return static_cast<std::uint16_t>(std::to_integer<unsigned>(at[0]) |
                                              (std::to_integer<unsigned>(at[1]) << 8U));
        }
        [[nodiscard]] std::uint32_t load_u32(const std::byte *at) noexcept {
            std::uint32_t value = 0U;
            for (std::size_t i = 0U; i < 4U; ++i) {
                value |= static_cast<std::uint32_t>(std::to_integer<unsigned>(at[i])) << (8U * i);
            }
            return value;
        }
        [[nodiscard]] std::uint64_t load_u64(const std::byte *at) noexcept {
            std::uint64_t value = 0U;
            for (std::size_t i = 0U; i < 8U; ++i) {
                value |= static_cast<std::uint64_t>(std::to_integer<unsigned>(at[i])) << (8U * i);
            }
            return value;
        }
    }  // namespace

    void zero_bytes(std::span<std::byte> buffer) noexcept {
        /* volatile on purpose: a plain fill of a buffer nobody reads again is exactly what an
         *  optimiser is allowed to delete, and this one holds credentials. */
        for (std::size_t i = 0U; i < buffer.size(); ++i) {
            volatile std::byte *cell = &buffer[i];
            *cell = std::byte{0};
        }
    }

    FrameKey frame_key_of(std::uint16_t raw) noexcept {
        switch (static_cast<FrameKey>(raw)) {
            case FrameKey::Backend: return FrameKey::Backend;
            case FrameKey::Route: return FrameKey::Route;
            case FrameKey::Phase: return FrameKey::Phase;
            case FrameKey::Generation: return FrameKey::Generation;
            case FrameKey::Address: return FrameKey::Address;
            case FrameKey::Value: return FrameKey::Value;
            case FrameKey::Form: return FrameKey::Form;
            case FrameKey::Offset: return FrameKey::Offset;
            case FrameKey::Length: return FrameKey::Length;
            case FrameKey::Token: return FrameKey::Token;
            case FrameKey::Attempts: return FrameKey::Attempts;
            case FrameKey::SettleUs: return FrameKey::SettleUs;
            case FrameKey::DelayMs: return FrameKey::DelayMs;
            case FrameKey::PollMs: return FrameKey::PollMs;
            case FrameKey::Mode: return FrameKey::Mode;
            case FrameKey::Flags: return FrameKey::Flags;
            case FrameKey::Kind: return FrameKey::Kind;
            case FrameKey::Index: return FrameKey::Index;
            case FrameKey::Width: return FrameKey::Width;
            case FrameKey::Count: return FrameKey::Count;
            case FrameKey::Bytes: return FrameKey::Bytes;
            case FrameKey::Path: return FrameKey::Path;
            case FrameKey::Text: return FrameKey::Text;
            case FrameKey::Secret: return FrameKey::Secret;
            case FrameKey::Image: return FrameKey::Image;
            case FrameKey::CountKey: break;
            case FrameKey::PageBase: return FrameKey::PageBase;
            case FrameKey::Word0: return FrameKey::Word0;
            case FrameKey::Word1: return FrameKey::Word1;
            case FrameKey::PayloadPage: return FrameKey::PayloadPage;
            case FrameKey::PayloadBase: return FrameKey::PayloadBase;
            case FrameKey::AliasDelta: return FrameKey::AliasDelta;
            case FrameKey::Found: return FrameKey::Found;
            case FrameKey::VictimTask: return FrameKey::VictimTask;
            case FrameKey::VictimPid: return FrameKey::VictimPid;
            case FrameKey::Sequence: return FrameKey::Sequence;
            case FrameKey::BranchWord: return FrameKey::BranchWord;
            case FrameKey::DisplacedWord: return FrameKey::DisplacedWord;
            case FrameKey::SiteOffset: return FrameKey::SiteOffset;
            case FrameKey::LandingOffset: return FrameKey::LandingOffset;
        }
        return FrameKey::CountKey;
    }

    bool FrameWriter::begin(FrameOp op) noexcept {
        usable_ = false;
        entries_ = 0U;
        size_ = 0U;
        if (buffer_.size() < kFrameHeaderSize || op == FrameOp::Count) {
            return false;
        }
        std::byte *at = buffer_.data();
        store_u32(at, kFrameMagic);
        store_u16(at + 4U, kFrameVersion);
        store_u16(at + 6U, static_cast<std::uint16_t>(op));
        store_u32(at + 8U, 0U);
        store_u32(at + 12U, 0U);
        size_ = kFrameHeaderSize;
        usable_ = true;
        return true;
    }

    bool FrameWriter::put_u64(FrameKey key, std::uint64_t value) noexcept {
        if (!usable_ || key == FrameKey::CountKey || entries_ >= kFrameMaxEntries ||
            size_ + 16U > kFrameMaxBytes || size_ + 16U > buffer_.size()) {
            usable_ = false;
            return false;
        }
        std::byte *at = buffer_.data() + size_;
        store_u16(at, static_cast<std::uint16_t>(key));
        at[2] = static_cast<std::byte>(FrameKind::U64);
        at[3] = std::byte{0};
        store_u32(at + 4U, 8U);
        store_u64(at + 8U, value);
        size_ += 16U;
        ++entries_;
        return true;
    }

    bool FrameWriter::put_bytes(FrameKey key, std::span<const std::byte> payload) noexcept {
        if (!usable_ || key == FrameKey::CountKey || entries_ >= kFrameMaxEntries ||
            payload.size() > kFrameMaxBytes || size_ + 8U + payload.size() > kFrameMaxBytes ||
            size_ + 8U + payload.size() > buffer_.size()) {
            usable_ = false;
            return false;
        }
        std::byte *at = buffer_.data() + size_;
        store_u16(at, static_cast<std::uint16_t>(key));
        at[2] = static_cast<std::byte>(FrameKind::Bytes);
        at[3] = std::byte{0};
        store_u32(at + 4U, static_cast<std::uint32_t>(payload.size()));
        if (!payload.empty()) {
            std::memcpy(at + 8U, payload.data(), payload.size());
        }
        size_ += 8U + payload.size();
        ++entries_;
        return true;
    }

    bool FrameWriter::finish(FrameStatus status) noexcept {
        if (!usable_) {
            return false;
        }
        std::byte *at = buffer_.data();
        store_u32(at + 8U, static_cast<std::uint32_t>(status));
        store_u32(at + 12U, entries_);
        usable_ = false;
        return true;
    }

    FrameStatus FrameReader::open(std::span<const std::byte> frame, FrameOp &op) noexcept {
        frame_ = frame;
        entries_ = 0U;
        if (frame.size() < kFrameHeaderSize) {
            return FrameStatus::Malformed;
        }
        const std::byte *at = frame.data();
        if (load_u32(at) != kFrameMagic) {
            return FrameStatus::Malformed;
        }
        if (load_u16(at + 4U) != kFrameVersion) {
            return FrameStatus::VersionMismatch;
        }
        const FrameOp asked = frame_op_of(load_u16(at + 6U));
        if (asked == FrameOp::Count) {
            return FrameStatus::OpUnknown;
        }
        const std::uint32_t declared = load_u32(at + 12U);
        if (declared > kFrameMaxEntries) {
            return FrameStatus::Malformed;
        }
        std::size_t cursor = kFrameHeaderSize;
        for (std::uint32_t i = 0U; i < declared; ++i) {
            if (cursor + 8U > frame.size()) {
                return FrameStatus::Malformed;
            }
            const std::uint16_t raw_key = load_u16(at + cursor);
            const std::uint8_t raw_kind = std::to_integer<std::uint8_t>(at[cursor + 2U]);
            if (raw_kind != static_cast<std::uint8_t>(FrameKind::U64) &&
                raw_kind != static_cast<std::uint8_t>(FrameKind::Bytes)) {
                return FrameStatus::Malformed;
            }
            const std::uint32_t length = load_u32(at + cursor + 4U);
            if (length > kFrameMaxBytes || cursor + 8U + length > frame.size()) {
                return FrameStatus::Malformed;
            }
            if (raw_kind == static_cast<std::uint8_t>(FrameKind::U64) && length != 8U) {
                return FrameStatus::KeyWidth;
            }
            const FrameKey key = frame_key_of(raw_key);
            if (key == FrameKey::CountKey) {
                return FrameStatus::KeyUnknown;
            }
            for (std::uint32_t seen = 0U; seen < entries_; ++seen) {
                if (table_[seen].key == key) {
                    return FrameStatus::KeyDuplicate;
                }
            }
            Entry &slot = table_[entries_];
            slot.key = key;
            slot.kind = static_cast<FrameKind>(raw_kind);
            slot.payload = cursor + 8U;
            slot.length = length;
            ++entries_;
            cursor += 8U + length;
        }
        if (cursor != frame.size()) {
            /* Trailing bytes mean the sender and this reader disagree about the frame's length,
             *  which is exactly the mistake that must not be read as "the rest was empty". */
            return FrameStatus::Malformed;
        }
        op = asked;
        return FrameStatus::Ok;
    }

    bool FrameReader::has(FrameKey key) const noexcept {
        for (std::uint32_t i = 0U; i < entries_; ++i) {
            if (table_[i].key == key) {
                return true;
            }
        }
        return false;
    }

    bool FrameReader::find_u64(FrameKey key, std::uint64_t &out) const noexcept {
        for (std::uint32_t i = 0U; i < entries_; ++i) {
            if (table_[i].key == key && table_[i].kind == FrameKind::U64) {
                out = load_u64(frame_.data() + table_[i].payload);
                return true;
            }
        }
        return false;
    }

    bool FrameReader::find_bytes(FrameKey key, std::span<const std::byte> &out) const noexcept {
        for (std::uint32_t i = 0U; i < entries_; ++i) {
            if (table_[i].key == key && table_[i].kind == FrameKind::Bytes) {
                out = frame_.subspan(table_[i].payload, table_[i].length);
                return true;
            }
        }
        return false;
    }
}  // namespace ghostlock::jni
