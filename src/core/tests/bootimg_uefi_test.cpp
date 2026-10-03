/*
 * bootimg uefi probe: the runtime cross-checks a profile's physical load base
 * against the UEFI image the app staged. This pins the parser's contract:
 *
 *   - the base is the hex pair on the line that ends with the `"Kernel"` label;
 *   - exactly one distinct base is an answer, several are not (an image holding
 *     maps for more than one target must not be resolved by picking one);
 *   - no label, or a label whose line carries no pair, is "no answer" rather
 *     than a guess — a wrong base would move every write the exploit performs.
 *
 * The fixtures below are the shape a real firmware prints, including the
 * whitespace variants, plus the conflict case.
 */
#include "bootimg/physmap.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
    std::vector<std::uint8_t> bytes_of(const std::string &text) {
        return {text.begin(), text.end()};
    }

    bool probe_equals(const std::string &text, std::uint64_t expected) {
        const auto blob = bytes_of(text);
        const auto value = ghostlock::bootimg::kernel_phys_load_from_uefi(blob);
        return value.has_value() && *value == expected;
    }

    bool probe_gives_no_answer(const std::string &text) {
        const auto blob = bytes_of(text);
        return !ghostlock::bootimg::kernel_phys_load_from_uefi(blob).has_value();
    }
} // namespace

int32_t main(void) {
    /* The ordinary entry, embedded in a larger map. */
    const std::string one_entry =
            "memmap {\n"
            "  0x00000000B0000000, 0x0000000000100000, \"Reserved\"\n"
            "  0x0000000080000000, 0x0000000002000000, \"Kernel\"\n"
            "  0x0000000084000000, 0x0000000000100000, \"Ramdisk\"\n"
            "}\n";
    if (!probe_equals(one_entry, 0x80000000ULL)) {
        puts("bootimg_uefi_test: single entry not parsed");
        return 1;
    }

    /* Tab-separated and unpadded hex are the same line to the firmware. */
    if (!probe_equals("0x80000000,\t0x2000000,\t\"Kernel\"\n", 0x80000000ULL)) {
        puts("bootimg_uefi_test: whitespace/padding variant not parsed");
        return 1;
    }

    /* The label alone carries no base. */
    if (!probe_gives_no_answer("  \"Kernel\"\n")) {
        puts("bootimg_uefi_test: bare label accepted");
        return 1;
    }

    /* A label whose line has no 0x pair must not borrow a neighbour's value. */
    if (!probe_gives_no_answer("0x0000000090000000, 0x1000, \"Other\"\n  \"Kernel\"\n")) {
        puts("bootimg_uefi_test: label without a pair accepted");
        return 1;
    }

    /* Two distinct bases: the image describes more than one target. */
    const std::string conflict =
            "0x0000000080000000, 0x2000000, \"Kernel\"\n"
            "0x00000000A0000000, 0x2000000, \"Kernel\"\n";
    if (!probe_gives_no_answer(conflict)) {
        puts("bootimg_uefi_test: conflicting bases accepted");
        return 1;
    }

    /* The same base twice is one answer, not a conflict. */
    const std::string repeated =
            "0x0000000080000000, 0x2000000, \"Kernel\"\n"
            "0x0000000080000000, 0x2000000, \"Kernel\"\n";
    if (!probe_equals(repeated, 0x80000000ULL)) {
        puts("bootimg_uefi_test: repeated base treated as a conflict");
        return 1;
    }

    /* Truncated / absent input stays unanswered. */
    if (!probe_gives_no_answer("") ||
        !probe_gives_no_answer("0x0000000080000000, 0x2000000, \"Ker")) {
        puts("bootimg_uefi_test: truncated input answered");
        return 1;
    }

    /* A base of zero is not a load address. */
    if (!probe_gives_no_answer("0x0000000000000000, 0x2000000, \"Kernel\"\n")) {
        puts("bootimg_uefi_test: zero base accepted");
        return 1;
    }

    puts("bootimg_uefi_test: ok");
    return 0;
}
