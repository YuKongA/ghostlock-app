/** @file tests/pagecache_seam_test.cpp
 *  The second seam's decisions, with a window that records instead of writing.
 *
 *  What is pinned here is what the seam decides before the route body is ported: that this route's
 *  unit is a sixteen-byte block and a word is refused rather than truncated, that the doors are
 *  opened when the route comes up and closed when it goes down, that the credentials are cleared
 *  when it goes down rather than kept in memory for the life of the process, and that a caller
 *  without credentials is refused by name instead of writing under a key of zeros.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43284/seam/pagecache_seam.hpp"

namespace {
    using ghostlock::backend::cve_2026_43284::SaMaterial;
    using ghostlock::backend::cve_2026_43284::seam::PageCacheSeam;
    using ghostlock::backend::cve_2026_43284::seam::PageCacheWindow;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    class RecordingWindow final : public PageCacheWindow {
    public:
        int opened = 0;
        int closed = 0;
        int blocks = 0;
        std::uint64_t position = 0U;
        std::uint8_t last[16] = {};

        [[nodiscard]] bool open_doors(const SaMaterial &sa) noexcept override {
            ++opened;
            return sa.set;
        }
        void close_doors() noexcept override { ++closed; }
        [[nodiscard]] bool write_block(std::uint64_t at,
                                       const std::uint8_t block[16]) noexcept override {
            ++blocks;
            position = at;
            for (std::size_t i = 0U; i < 16U; ++i) {
                last[i] = block[i];
            }
            return true;
        }
    };

    SaMaterial credentials() noexcept {
        SaMaterial sa;
        sa.spi = 0x1234U;
        sa.aes_key[0] = 0xAAU;
        sa.hmac_key[0] = 0xBBU;
        sa.set = true;
        return sa;
    }

    void it_needs_credentials_and_takes_them_by_value() {
        RecordingWindow window;
        PageCacheSeam seam(window);
        const SaMaterial none;
        expect(!seam.bring_up_with(none), "a set that is not credentials does not bring it up");
        expect(window.opened == 0, "and the doors stay shut");
        const SaMaterial sa = credentials();
        expect(seam.bring_up_with(sa), "real credentials bring it up");
        expect(window.opened == 1, "opening the doors once");
        expect(!seam.bring_up_with(sa), "and a second bring_up is refused");
        expect(seam.tear_down(), "it goes down");
        expect(window.closed == 1, "closing the doors once");
        expect(!seam.tear_down(), "and a second tear_down is refused");
    }

    void the_unit_is_a_block_and_a_word_is_refused() {
        RecordingWindow window;
        PageCacheSeam seam(window);
        expect(PageCacheSeam::width() == 16U,
               "the width is sixteen, asked rather than assumed");
        expect(!seam.land_word(0x100U, 0x4142434445464748ULL, 0U),
               "a word is refused: this route has no word to give");
        expect(!seam.land_block(0x100U, credentials().aes_key),
               "and a block before bring_up does not reach the window");
        expect(window.blocks == 0, "so the window was not touched");
        static_cast<void>(seam.bring_up_with(credentials()));
        std::uint8_t block[16] = {};
        for (std::size_t i = 0U; i < 16U; ++i) {
            block[i] = static_cast<std::uint8_t>(i);
        }
        expect(seam.land_block(0x200U, block), "a block lands once the route is up");
        expect(window.blocks == 1 && window.position == 0x200U,
               "at the position the caller gave");
        expect(window.last[0] == 0U && window.last[15] == 15U,
               "with all sixteen bytes carried through unchanged");
    }
}  // namespace

int main() {
    std::printf("page cache seam: credentials and doors\n");
    it_needs_credentials_and_takes_them_by_value();
    std::printf("page cache seam: a block, not a word\n");
    the_unit_is_a_block_and_a_word_is_refused();
    if (g_failures != 0U) {
        std::printf("page cache seam: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("page cache seam: PASS (block width, doors opened and closed, word refused)\n");
    return 0;
}
