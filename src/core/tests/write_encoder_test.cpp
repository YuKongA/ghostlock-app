/** @file tests/write_encoder_test.cpp
 *  The arm rules and the all-or-nothing write.
 *
 *  Sections: 1. a value write aims at the value, a leaf write at the target,
 *  and the side fields are zero either way; 2. the arm lands at the declared
 *  offsets, byte for byte; 3. a fragment that cannot hold the arm is refused and
 *  left untouched, and an impossible width is refused by name.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "memory/write_encoder.hpp"

namespace {
    using ghostlock::memory::arm_for;
    using ghostlock::memory::encode_verdict_name;
    using ghostlock::memory::encode_waiter_arm;
    using ghostlock::memory::EncodeVerdict;
    using ghostlock::memory::WaiterFieldOffsets;
    using ghostlock::memory::WriteArm;
    using ghostlock::memory::WriteMode;
    using ghostlock::memory::WriteRequest;

    constexpr std::size_t kPointer = 8U;
    constexpr std::uint64_t kTarget = 0xFFFF888100001000ULL;
    constexpr std::uint64_t kValue = 0xFFFF888100002000ULL;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    WriteRequest request(bool preserve, std::uint64_t target) {
        WriteRequest made;
        made.target = target;
        made.mode = WriteMode::Credential;
        made.preserve_child = preserve;
        return made;
    }

    /* ---- 1: the arm rules ---- */
    void the_arm_rules_hold() {
        const WriteArm value_write = arm_for(request(true, kTarget), kValue, kPointer);
        expect(value_write.pc == kValue, "a value write aims the fragment at the value");
        expect(value_write.left == kTarget, "and carries the target in the left field");
        expect(value_write.right == 0U, "with both side fields zero");

        const WriteArm leaf_write = arm_for(request(false, kTarget), kValue, kPointer);
        expect(leaf_write.pc == kTarget - kPointer,
               "a leaf write aims one pointer width before the target");
        expect(leaf_write.left == kTarget, "and still carries the target");
        expect(leaf_write.right == 0U, "with the side fields zero here too");
        expect(leaf_write.pc != value_write.pc, "the two arms differ");

        const WriteArm tiny = arm_for(request(false, 4U), kValue, kPointer);
        expect(tiny.pc == 0U, "a target too small to step back from does not wrap around");
        const WriteArm no_width = arm_for(request(true, kTarget), kValue, 0U);
        expect(no_width.pc == 0U && no_width.left == 0U && no_width.right == 0U,
               "a zero width produces an empty arm rather than a wrong one");
    }

    /* ---- 2: the arm lands where the profile says ---- */
    void the_arm_lands() {
        WaiterFieldOffsets offsets;
        offsets.pc = 0U;
        offsets.right = 16U;
        offsets.left = 24U;
        std::uint8_t fragment[40];
        std::memset(fragment, 0xAA, sizeof(fragment));
        const WriteArm arm = arm_for(request(true, kTarget), kValue, kPointer);
        expect(encode_waiter_arm(fragment, sizeof(fragment), offsets, arm, kPointer) ==
                   EncodeVerdict::Ok,
               "the arm fits a fragment of this size");
        std::uint64_t read_pc = 0U;
        std::uint64_t read_right = 0U;
        std::uint64_t read_left = 0U;
        std::memcpy(&read_pc, fragment + offsets.pc, kPointer);
        std::memcpy(&read_right, fragment + offsets.right, kPointer);
        std::memcpy(&read_left, fragment + offsets.left, kPointer);
        expect(read_pc == arm.pc, "the pc word is where the offsets say");
        expect(read_right == 0U && read_left == arm.left, "and so are the two side words");
        expect(fragment[8] == 0xAA && fragment[32] == 0xAA,
               "nothing outside the three words was touched");
    }

    /* ---- 3: what does not fit is not written ---- */
    void refusals_leave_the_fragment_alone() {
        WaiterFieldOffsets offsets;
        offsets.pc = 0U;
        offsets.right = 16U;
        offsets.left = 24U;
        const WriteArm arm = arm_for(request(true, kTarget), kValue, kPointer);
        std::uint8_t fragment[31];
        std::memset(fragment, 0xAA, sizeof(fragment));
        const EncodeVerdict verdict =
            encode_waiter_arm(fragment, sizeof(fragment), offsets, arm, kPointer);
        expect(verdict == EncodeVerdict::FragmentTooSmall, "a short fragment is refused");
        expect(std::string_view(encode_verdict_name(verdict)) == std::string_view("budget-exceeded"),
               "and the refusal is named");
        bool untouched = true;
        for (const std::uint8_t byte : fragment) {
            if (byte != 0xAA) {
                untouched = false;
            }
        }
        expect(untouched, "and not one byte of it was written");

        expect(encode_waiter_arm(nullptr, 0U, offsets, arm, kPointer) ==
                   EncodeVerdict::FragmentTooSmall,
               "no fragment is refused");
        std::uint8_t ok[40];
        expect(encode_waiter_arm(ok, sizeof(ok), offsets, arm, 0U) == EncodeVerdict::WidthInvalid,
               "a zero width is refused");
        expect(encode_waiter_arm(ok, sizeof(ok), offsets, arm, 16U) == EncodeVerdict::WidthInvalid,
               "a width wider than a word is refused");
        expect(std::string_view(encode_verdict_name(EncodeVerdict::WidthInvalid)) ==
                   std::string_view("width-mismatch"),
               "and that refusal is named too");
        WaiterFieldOffsets late;
        late.left = 36U;
        std::uint8_t guard[40];
        std::memset(guard, 0x55, sizeof(guard));
        expect(encode_waiter_arm(guard, sizeof(guard), late, arm, kPointer) ==
                   EncodeVerdict::FragmentTooSmall,
               "a field that runs past the end is refused");
    }
}  // namespace

int main() {
    std::printf("write encoder: arm rules\n");
    the_arm_rules_hold();
    std::printf("write encoder: landing\n");
    the_arm_lands();
    std::printf("write encoder: refusals\n");
    refusals_leave_the_fragment_alone();
    if (g_failures != 0U) {
        std::printf("write encoder: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("write encoder: PASS (arm rules + bounded write + named refusals)\n");
    return 0;
}
