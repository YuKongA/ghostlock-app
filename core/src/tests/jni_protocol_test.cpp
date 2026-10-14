/** @file tests/jni_protocol_test.cpp
 *  The boundary wire: round trip, and every named refusal the codec owns.
 *
 *  What it pins: a frame that this build writes is a frame this build reads (values included);
 *  a frame whose shape is wrong is refused with the status that names the reason, never half-read;
 *  and a buffer that held a secret comes back zeroed.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

#include "jni/session_protocol.hpp"

namespace {
    using ghostlock::jni::FrameKey;
    using ghostlock::jni::FrameOp;
    using ghostlock::jni::FrameReader;
    using ghostlock::jni::FrameStatus;
    using ghostlock::jni::FrameWriter;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    void put_u16(std::byte *at, std::uint16_t value) {
        at[0] = static_cast<std::byte>(value & 0xffU);
        at[1] = static_cast<std::byte>((value >> 8U) & 0xffU);
    }
    void put_u32(std::byte *at, std::uint32_t value) {
        for (std::size_t i = 0U; i < 4U; ++i) {
            at[i] = static_cast<std::byte>((value >> (8U * i)) & 0xffU);
        }
    }
    void put_u64(std::byte *at, std::uint64_t value) {
        for (std::size_t i = 0U; i < 8U; ++i) {
            at[i] = static_cast<std::byte>((value >> (8U * i)) & 0xffU);
        }
    }

    /** A frame built by hand, so a defect can be placed exactly. */
    struct Raw final {
        std::byte bytes[256]{};
        std::size_t size = 0U;
        void header(FrameOp op, std::uint32_t entries, std::uint16_t version = 1U,
                    std::uint32_t magic = 0x4A4B4C47U) {
            put_u32(bytes, magic);
            put_u16(bytes + 4U, version);
            put_u16(bytes + 6U, static_cast<std::uint16_t>(op));
            put_u32(bytes + 8U, 0U);
            put_u32(bytes + 12U, entries);
            size = 16U;
        }
        void entry(FrameKey key, std::uint8_t kind, std::uint32_t length, std::uint32_t declared) {
            put_u16(bytes + size, static_cast<std::uint16_t>(key));
            bytes[size + 2U] = static_cast<std::byte>(kind);
            bytes[size + 3U] = std::byte{0};
            put_u32(bytes + size + 4U, declared);
            size += 8U + length;
        }
    };

    void round_trip_keeps_every_value() {
        std::byte buffer[512]{};
        FrameWriter writer{std::span<std::byte>(buffer)};
        expect(writer.begin(FrameOp::WriteWord), "a frame starts");
        expect(writer.put_u64(FrameKey::Address, 0xffffff8042abcdefULL), "an address goes in");
        expect(writer.put_u64(FrameKey::Value, 0x0000000000000000ULL), "a zero value goes in");
        expect(writer.put_u64(FrameKey::Form, 2U), "a form goes in");
        const std::byte blob[3] = {std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}};
        expect(writer.put_bytes(FrameKey::Bytes, std::span<const std::byte>(blob)), "bytes go in");
        expect(writer.finish(FrameStatus::Ok), "the frame closes");

        FrameReader reader;
        FrameOp op = FrameOp::Snapshot;
        expect(reader.open(writer.view(), op) == FrameStatus::Ok, "the frame opens");
        expect(op == FrameOp::WriteWord, "the op survives");
        std::uint64_t address = 0U;
        expect(reader.find_u64(FrameKey::Address, address) && address == 0xffffff8042abcdefULL,
               "the address survives");
        std::uint64_t zero = 1U;
        expect(reader.find_u64(FrameKey::Value, zero) && zero == 0U, "a written zero reads as zero");
        std::span<const std::byte> back{};
        expect(reader.find_bytes(FrameKey::Bytes, back) && back.size() == 3U &&
                   back[2] == std::byte{0xCC},
               "the bytes survive");
        expect(!reader.has(FrameKey::Token), "an absent key is absent");
        expect(reader.entry_count() == 4U, "the entry count is the number written");
    }

    void an_empty_request_is_legal() {
        std::byte buffer[64]{};
        FrameWriter writer{std::span<std::byte>(buffer)};
        expect(writer.begin(FrameOp::Snapshot), "an empty request starts");
        expect(writer.finish(FrameStatus::Ok), "an empty request closes");
        FrameReader reader;
        FrameOp op = FrameOp::Count;
        expect(reader.open(writer.view(), op) == FrameStatus::Ok, "an empty request opens");
        expect(op == FrameOp::Snapshot && reader.entry_count() == 0U, "and it is the op asked for");
    }

    void the_header_is_checked_first() {
        Raw raw;
        raw.header(FrameOp::Snapshot, 0U);
        FrameReader reader;
        FrameOp op = FrameOp::Count;
        expect(reader.open(std::span<const std::byte>(raw.bytes, 4U), op) == FrameStatus::Malformed,
               "a buffer shorter than the header is malformed");
        Raw bad_magic;
        bad_magic.header(FrameOp::Snapshot, 0U, 1U, 0x12345678U);
        expect(reader.open(std::span<const std::byte>(bad_magic.bytes, 16U), op) ==
                   FrameStatus::Malformed,
               "a wrong magic is malformed");
        Raw bad_version;
        bad_version.header(FrameOp::Snapshot, 0U, 9U);
        expect(reader.open(std::span<const std::byte>(bad_version.bytes, 16U), op) ==
                   FrameStatus::VersionMismatch,
               "a version this build does not read is refused by name");
        Raw bad_op;
        bad_op.header(static_cast<FrameOp>(99U), 0U);
        expect(reader.open(std::span<const std::byte>(bad_op.bytes, 16U), op) ==
                   FrameStatus::OpUnknown,
               "an op outside the table is refused by name");
        Raw too_many;
        too_many.header(FrameOp::Snapshot, 65U);
        expect(reader.open(std::span<const std::byte>(too_many.bytes, 16U), op) ==
                   FrameStatus::Malformed,
               "more entries than the limit is malformed");
    }

    void the_entries_are_checked_one_by_one() {
        FrameReader reader;
        FrameOp op = FrameOp::Count;
        Raw unknown;
        unknown.header(FrameOp::Snapshot, 1U);
        unknown.entry(static_cast<FrameKey>(999U), 0U, 8U, 8U);
        unknown.size = 16U + 8U + 8U;
        expect(reader.open(std::span<const std::byte>(unknown.bytes, unknown.size), op) ==
                   FrameStatus::KeyUnknown,
               "a key outside the table is refused by name");

        Raw duplicate;
        duplicate.header(FrameOp::Snapshot, 2U);
        duplicate.entry(FrameKey::Address, 0U, 8U, 8U);
        duplicate.entry(FrameKey::Address, 0U, 8U, 8U);
        duplicate.size = 16U + 2U * (8U + 8U);
        expect(reader.open(std::span<const std::byte>(duplicate.bytes, duplicate.size), op) ==
                   FrameStatus::KeyDuplicate,
               "the same key twice is refused by name");

        Raw width;
        width.header(FrameOp::Snapshot, 1U);
        width.entry(FrameKey::Address, 0U, 4U, 4U);
        width.size = 16U + 8U + 4U;
        expect(reader.open(std::span<const std::byte>(width.bytes, width.size), op) ==
                   FrameStatus::KeyWidth,
               "a u64 entry that is not eight bytes is refused by width");

        Raw truncated;
        truncated.header(FrameOp::Snapshot, 1U);
        truncated.entry(FrameKey::Address, 0U, 8U, 8U);
        truncated.size = 16U + 8U + 4U;   /* the payload promised eight and four arrived */
        expect(reader.open(std::span<const std::byte>(truncated.bytes, truncated.size), op) ==
                   FrameStatus::Malformed,
               "a short payload is malformed");

        Raw trailing;
        trailing.header(FrameOp::Snapshot, 0U);
        trailing.size = 20U;   /* four bytes nobody declared */
        expect(reader.open(std::span<const std::byte>(trailing.bytes, trailing.size), op) ==
                   FrameStatus::Malformed,
               "trailing bytes are malformed, not ignored");

        Raw bad_kind;
        bad_kind.header(FrameOp::Snapshot, 1U);
        bad_kind.entry(FrameKey::Address, 7U, 8U, 8U);
        bad_kind.size = 16U + 8U + 8U;
        expect(reader.open(std::span<const std::byte>(bad_kind.bytes, bad_kind.size), op) ==
                   FrameStatus::Malformed,
               "a kind outside the table is malformed");
    }

    void a_frame_that_does_not_fit_is_refused() {
        std::byte small[20]{};
        FrameWriter writer{std::span<std::byte>(small)};
        expect(writer.begin(FrameOp::Snapshot), "the header fits");
        expect(!writer.put_u64(FrameKey::Address, 1U), "an entry that does not fit is refused");
        expect(!writer.usable(), "and the frame is not usable afterwards");
        expect(!writer.finish(FrameStatus::Ok), "a refused frame cannot be closed as a frame");
    }

    void a_secret_buffer_comes_back_zeroed() {
        std::byte secret[32]{};
        std::memset(secret, 0xAB, sizeof(secret));
        ghostlock::jni::zero_bytes(std::span<std::byte>(secret));
        std::size_t nonzero = 0U;
        for (std::size_t i = 0U; i < sizeof(secret); ++i) {
            volatile std::byte *cell = &secret[i];
            if (*cell != std::byte{0}) {
                ++nonzero;
            }
        }
        expect(nonzero == 0U, "zero_bytes leaves nothing behind");
    }
}  // namespace

int main() {
    std::printf("jni protocol: round trip\n");
    round_trip_keeps_every_value();
    std::printf("jni protocol: empty request\n");
    an_empty_request_is_legal();
    std::printf("jni protocol: header\n");
    the_header_is_checked_first();
    std::printf("jni protocol: entries\n");
    the_entries_are_checked_one_by_one();
    std::printf("jni protocol: room\n");
    a_frame_that_does_not_fit_is_refused();
    std::printf("jni protocol: secrets\n");
    a_secret_buffer_comes_back_zeroed();
    if (g_failures != 0U) {
        std::printf("jni protocol: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("jni protocol: PASS (round trip + every named refusal)\n");
    return 0;
}
