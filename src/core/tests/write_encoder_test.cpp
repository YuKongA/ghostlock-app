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
        expect(leaf_write.left == 0U,
               "and leaves the side fields zero, which is how the target is aimed at");
        expect(leaf_write.right == 0U, "with the other side field zero as well");
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
    /* ---- 1b: the tree's own fixed vectors ---- */
    /* These are the values the retired tree pinned for the same three cases: a
     * leaf write aims before the target with both side fields zero, and the two
     * value writes aim at the value the caller supplies, which is the page-derived
     * one for a zero write and the initial credential alias for a credential one. */
    void the_fixed_vectors_hold() {
        constexpr std::uint64_t kPage = 0xffffff8800210000ULL;
        constexpr std::uint64_t kInitCred = 0xffffff802abfd588ULL;
        struct Vector final {
            std::uint64_t target;
            WriteMode mode;
            bool preserve;
            std::uint64_t value;
            std::uint64_t expected_pc;
            std::uint64_t expected_left;
        };
        const Vector vectors[] = {
            {0xffffff8000123000ULL, WriteMode::Zero, false, 0U, 0xffffff8000122ff8ULL, 0U},
            {0xffffff8000124000ULL, WriteMode::Zero, true, kPage + 0x100ULL,
             0xffffff8800210100ULL, 0xffffff8000124000ULL},
            {0xffffff8000125000ULL, WriteMode::Credential, true, kInitCred,

             0xffffff802abfd588ULL, 0xffffff8000125000ULL},
        };
        for (const Vector &vector : vectors) {
            WriteRequest made;
            made.target = vector.target;
            made.mode = vector.mode;
            made.preserve_child = vector.preserve;
            const WriteArm arm = arm_for(made, vector.value, kPointer);
            expect(arm.pc == vector.expected_pc, "the vector's pc word is reproduced");
            expect(arm.left == vector.expected_left, "and its left word");
            expect(arm.right == 0U, "and the side field is zero in every vector");
        }
    }
}  // namespace

int main() {
    std::printf("write encoder: arm rules\n");
    the_arm_rules_hold();
    std::printf("write encoder: the fixed vectors\n");
    the_fixed_vectors_hold();
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
