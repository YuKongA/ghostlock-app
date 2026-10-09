/** @file tests/multicast_arm_test.cpp
 *  The multicast geometry and the two words it places.
 *
 *  Sections: 1. the required size is derived from the two fields, whichever sits
 *  later; 2. a geometry that puts a field outside the buffer, or two fields on
 *  each other, is refused by name; 3. the arm lands where the geometry says, and
 *  a refusal leaves the buffer exactly as it was.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "memory/multicast_arm.hpp"

namespace {
    using ghostlock::memory::ArmVerdict;
    using ghostlock::memory::arm_verdict_name;
    using ghostlock::memory::check_geometry;
    using ghostlock::memory::encode_multicast_arm;
    using ghostlock::memory::MulticastGeometry;
    using ghostlock::memory::required_bytes;

    constexpr std::size_t kWord = 8U;
    constexpr std::uint64_t kTask = 0xFFFF888100001000ULL;
    constexpr std::uint64_t kLock = 0xFFFF888100002000ULL;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** A geometry whose fields both fit in a buffer of this size. */
    MulticastGeometry geometry_for(std::size_t buffer_size) {
        MulticastGeometry geometry;
        geometry.waiter_offset = 0x20U;
        geometry.task_offset = 0x40U;
        geometry.lock_offset = 0x80U;
        geometry.buffer_size = buffer_size;
        return geometry;
    }

    /* ---- 1: how much buffer the geometry needs ---- */
    void the_required_size_follows_the_later_field() {
        const MulticastGeometry geometry = geometry_for(0x200U);
        expect(required_bytes(geometry, kWord) == 0x20U + 0x80U + kWord,
               "the later field decides the size");
        MulticastGeometry swapped = geometry;
        swapped.task_offset = 0x80U;
        swapped.lock_offset = 0x40U;
        expect(required_bytes(swapped, kWord) == required_bytes(geometry, kWord),
               "and it does not matter which of the two is later");
    }

    /* ---- 2: the geometry checks ---- */
    void bad_geometry_is_refused_by_name() {
        const MulticastGeometry geometry = geometry_for(0x200U);
        const std::size_t needed = required_bytes(geometry, kWord);
        expect(check_geometry(geometry, needed, kWord) == ArmVerdict::Ok,
               "a buffer of exactly the required size is enough");
        expect(check_geometry(geometry, needed - 1U, kWord) == ArmVerdict::OutOfBuffer,
               "one byte less is not");
        expect(std::string_view(arm_verdict_name(ArmVerdict::OutOfBuffer)) ==
                   std::string_view("budget-exceeded"),
               "and that refusal is named");

        MulticastGeometry overlapping = geometry;
        overlapping.lock_offset = overlapping.task_offset + 4U;
        expect(check_geometry(overlapping, 0x200U, kWord) == ArmVerdict::FieldsOverlap,
               "two fields within one word of each other cannot both be written");
        expect(std::string_view(arm_verdict_name(ArmVerdict::FieldsOverlap)) ==
                   std::string_view("param-invalid"),
               "and that one is named too");
        MulticastGeometry same = geometry;
        same.lock_offset = same.task_offset;
        expect(check_geometry(same, 0x200U, kWord) == ArmVerdict::FieldsOverlap,
               "the same offset twice is the same field, not two");
        expect(check_geometry(geometry, 0x200U, 0U) != ArmVerdict::Ok,
               "a width that is not a field width is refused");
        expect(check_geometry(geometry, 0x200U, 16U) != ArmVerdict::Ok,
               "so is a width wider than a word");
    }

    /* ---- 3: the words land, or nothing does ---- */
    void the_words_land_or_nothing_does() {
        const MulticastGeometry geometry = geometry_for(0x200U);
        std::uint8_t buffer[0x200];
        std::memset(buffer, 0x5A, sizeof(buffer));
        expect(encode_multicast_arm(buffer, sizeof(buffer), geometry, kTask, kLock, kWord) ==
                   ArmVerdict::Ok,
               "a valid geometry arms the buffer");
        std::uint64_t read_task = 0U;
        std::uint64_t read_lock = 0U;
        std::memcpy(&read_task, buffer + geometry.waiter_offset + geometry.task_offset, kWord);
        std::memcpy(&read_lock, buffer + geometry.waiter_offset + geometry.lock_offset, kWord);
        expect(read_task == kTask, "the fake task lands where task_offset says");
        expect(read_lock == kLock, "and the fake lock where lock_offset says");
        expect(buffer[0U] == 0x5AU && buffer[geometry.waiter_offset + geometry.task_offset - 1U] == 0x5AU,
               "nothing before the first field was touched");

        std::uint8_t refused[0x40];
        std::memset(refused, 0xC3, sizeof(refused));
        expect(encode_multicast_arm(refused, sizeof(refused), geometry, kTask, kLock, kWord) ==
                   ArmVerdict::OutOfBuffer,
               "a buffer too small for the geometry is refused");
        bool untouched = true;
        for (const std::uint8_t byte : refused) {
            if (byte != 0xC3) {
                untouched = false;
            }
        }
        expect(untouched, "and not one byte of it was written");
        expect(encode_multicast_arm(nullptr, 0U, geometry, kTask, kLock, kWord) == ArmVerdict::NoBuffer,
               "no buffer is refused");
    }
}  // namespace

int main() {
    std::printf("multicast arm: the required size\n");
    the_required_size_follows_the_later_field();
    std::printf("multicast arm: the geometry\n");
    bad_geometry_is_refused_by_name();
    std::printf("multicast arm: the words\n");
    the_words_land_or_nothing_does();
    if (g_failures != 0U) {
        std::printf("multicast arm: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("multicast arm: PASS (geometry checked + words placed + refusals inert)\n");
    return 0;
}
