/** @file tests/terminal_store_test.cpp
 *  A6-4 evidence: a validated payload lands where the terminal can run it.
 *
 *  Sections: 1. the bytes arrive intact under the final name, mode 0600, with no
 *  pending file left behind; 2. a payload of a size that needs several writes
 *  still arrives whole; 3. what cannot be stored is refused, and removal works.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include <sys/stat.h>
#include <unistd.h>

#include "terminal/payload_store.hpp"

namespace {
    using ghostlock::terminal::remove_payload;
    using ghostlock::terminal::StoreResult;
    using ghostlock::terminal::store_payload;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    bool read_back(const char *path, std::uint8_t *out, std::size_t size) {
        std::FILE *file = std::fopen(path, "rb");
        if (file == nullptr) {
            return false;
        }
        const std::size_t got = std::fread(out, 1U, size, file);
        std::fclose(file);
        return got == size;
    }

    /* ---- 1: the bytes arrive as written, and only they do ---- */
    void a_payload_lands_whole(const char *directory) {
        const std::uint8_t bytes[] = {0x10U, 0x20U, 0x30U, 0x40U, 0x50U};
        const StoreResult stored =
            store_payload(directory, "finish.bin", bytes, sizeof(bytes));
        expect(stored.ok, "a small payload is stored");
        expect(std::string_view(stored.path).find("finish.bin") != std::string_view::npos,
               "the result names the final path");

        struct stat info{};
        expect(::stat(stored.path, &info) == 0, "the payload exists where it was reported");
        expect((info.st_mode & 0777) == 0600, "the payload is mode 0600 and no wider");
        expect(static_cast<std::size_t>(info.st_size) == sizeof(bytes),
               "the payload is exactly the size that was written");

        std::uint8_t back[sizeof(bytes)];
        expect(read_back(stored.path, back, sizeof(back)), "the payload reads back");
        expect(std::memcmp(back, bytes, sizeof(bytes)) == 0, "the bytes are identical");

        char pending[256];
        std::snprintf(pending, sizeof(pending), "%s/finish.bin.pending", directory);
        expect(::access(pending, F_OK) != 0, "no pending file is left behind");
    }

    /* ---- 2: a payload that needs several writes ---- */
    void a_large_payload_lands_whole(const char *directory) {
        static std::uint8_t big[70000U];
        for (std::size_t i = 0U; i < sizeof(big); ++i) {
            big[i] = static_cast<std::uint8_t>(i % 251U);
        }
        const StoreResult stored = store_payload(directory, "big.bin", big, sizeof(big));
        expect(stored.ok, "a payload larger than one write is stored");
        static std::uint8_t back[sizeof(big)];
        expect(read_back(stored.path, back, sizeof(back)), "the large payload reads back");
        expect(std::memcmp(back, big, sizeof(big)) == 0, "the large bytes are identical");
    }

    /* ---- 3: refusals, and removal ---- */
    void refusals_and_removal(const char *directory) {
        const std::uint8_t bytes[] = {1U, 2U, 3U};
        expect(!store_payload(nullptr, "x.bin", bytes, sizeof(bytes)).ok,
               "no directory is refused");
        expect(!store_payload(directory, nullptr, bytes, sizeof(bytes)).ok,
               "no name is refused");
        expect(!store_payload(directory, "empty.bin", bytes, 0U).ok,
               "an empty payload is refused");
        expect(!store_payload(directory, "none.bin", nullptr, sizeof(bytes)).ok,
               "no bytes are refused");
        expect(std::string_view(store_payload(directory, "empty.bin", bytes, 0U).reason) ==
                   std::string_view("payload-missing"),
               "a refused store carries the registered name");
        const StoreResult stored = store_payload(directory, "gone.bin", bytes, sizeof(bytes));
        expect(stored.ok, "a payload to remove is stored first");
        expect(remove_payload(stored.path), "the payload is removed");
        expect(::access(stored.path, F_OK) != 0, "and it is gone");
        expect(!remove_payload(stored.path), "removing it twice reports failure");
    }
}  // namespace

int main() {
    char directory[] = "/tmp/ghostlock-store-XXXXXX";
    if (::mkdtemp(directory) == nullptr) {
        std::printf("FAIL: no temporary directory\n");
        return 1;
    }
    std::printf("store: a payload lands whole\n");
    a_payload_lands_whole(directory);
    std::printf("store: several writes\n");
    a_large_payload_lands_whole(directory);
    std::printf("store: refusals and removal\n");
    refusals_and_removal(directory);
    (void)::rmdir(directory);
    if (g_failures != 0U) {
        std::printf("store: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("store: PASS (atomic, 0600, intact, refusals named)\n");
    return 0;
}
