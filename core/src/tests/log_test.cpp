/** @file tests/log_test.cpp
 *  The logging landing: level filtering, the line format, and the file it actually writes.
 *
 *  Why this exists: the logger was rewritten (spdlog out, printf in) and its whole value is that a
 *  device log reads the way the greps expect. So the test drives the real sink -- a file -- and
 *  reads it back: the level letter, the numbers, the hex, and that a line below the minimum never
 *  arrives.
 */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "support/log.hpp"

namespace {
    constexpr const char *kPath = "../build/host/jni-log-test.txt";

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    std::string read_back() {
        std::FILE *file = std::fopen(kPath, "rb");
        if (file == nullptr) {
            return {};
        }
        std::string text;
        char buffer[256];
        std::size_t got = 0U;
        while ((got = std::fread(buffer, 1U, sizeof(buffer), file)) > 0U) {
            text.append(buffer, got);
        }
        (void)std::fclose(file);
        return text;
    }

    bool contains(const std::string &haystack, const char *needle) {
        return haystack.find(needle) != std::string::npos;
    }

    void a_line_carries_its_level_and_its_numbers() {
        (void)std::remove(kPath);
        expect(ghostlock::support::logging::open("info", kPath), "the sink opens");
        ghostlock::support::log("debug", "below the minimum");
        ghostlock::support::log("info", "plain line");
        ghostlock::support::log("info", "with a number", 7);
        ghostlock::support::log("warn", "with two numbers", 1, 2);
        ghostlock::support::log("error", "with four numbers", 1, 2, 3, 4);
        ghostlock::support::log("info", "with text", ghostlock::support::Text{"policy-name"});
        ghostlock::support::log("info", "with hex", ghostlock::support::Hex{0xabcU});
        expect(ghostlock::support::logging::close(), "the sink closes");
        expect(!ghostlock::support::logging::close(), "closing twice says so");

        const std::string text = read_back();
        expect(!text.empty(), "the file has content");
        expect(!contains(text, "below the minimum"), "a line under the minimum never arrives");
        expect(contains(text, "[I] plain line"), "an info line carries its letter");
        expect(contains(text, "with a number value=7"), "one number reads value=");
        expect(contains(text, "with two numbers a=1 b=2"), "two numbers read a= b=");
        expect(contains(text, "with four numbers a=1 b=2 c=3 d=4"), "four numbers keep their order");
        expect(contains(text, "with text policy-name"), "text is appended as itself");
        expect(contains(text, "with hex 0x0000000000000abc"), "hex is sixteen digits wide");

        /* The line format's head: HH:MM:SS.mmm [L] -- one shape, checked once. */
        const bool head_ok = text.size() > 22U && text[2] == ':' && text[5] == ':' && text[8] == '.' &&
                             text[12] == ' ' && text[13] == '[';
        expect(head_ok, "the head is HH:MM:SS.mmm [L]");
    }

    void nothing_is_written_before_open() {
        (void)std::remove(kPath);
        ghostlock::support::log("error", "before any sink");
        expect(read_back().empty(), "a line before open() leaves no file");
    }
}  // namespace

int main() {
    std::printf("log: before open\n");
    nothing_is_written_before_open();
    std::printf("log: line shape\n");
    a_line_carries_its_level_and_its_numbers();
    if (g_failures != 0U) {
        std::printf("log: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("log: PASS (levels, numbers, hex, the file itself)\n");
    return 0;
}
