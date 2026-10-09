/** @file tests/pagecache_chain_test.cpp
 *  The second backend's pieces wired into one, driven end to end.
 *
 *  What is checked is that they agree: the access comes up only with credentials, takes them from
 *  its own parameter set and refuses a set belonging to the other backend; the write goes down
 *  through the seam to the window as a sixteen-byte block at the position the parameters gave; a
 *  word is refused by width since this route has no word to give; and the route comes back down.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43284/kernel_access.hpp"

namespace {
    using ghostlock::backend::cve_2026_43284::Access;
    using ghostlock::backend::cve_2026_43284::AccessParams;
    using ghostlock::backend::cve_2026_43284::SaMaterial;
    using ghostlock::backend::cve_2026_43284::seam::PageCacheSeam;
    using ghostlock::backend::cve_2026_43284::seam::PageCacheWindow;
    using ghostlock::contract::AccessChannel;
    using ghostlock::contract::KernelAccess;

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
        std::uint8_t first_byte_of_secrets = 0U;

        [[nodiscard]] bool open_doors(const SaMaterial &sa) noexcept override {
            ++opened;
            first_byte_of_secrets = sa.aes_key[0];
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

    /** Parameters belonging to the other backend: same interface, different name. */
    class ForeignParams final : public KernelAccess::AccessMiscParameters {
    public:
        [[nodiscard]] std::string_view name() const noexcept override { return "cve_2026_43499"; }
    };

    void the_chain_writes_one_block() {
        RecordingWindow window;
        PageCacheSeam seam(window);
        Access access(seam);
        AccessParams params;
        params.sa = credentials();
        params.offset = 0x1000U;
        params.offset_set = true;
        expect(access.initialize(params).ok(), "credentials bring the route up through the seam");
        expect(window.opened == 1 && window.first_byte_of_secrets == 0xAAU,
               "and the doors were opened with the material the parameters carried");
        std::uint8_t bytes[16] = {};
        for (std::size_t i = 0U; i < 16U; ++i) {
            bytes[i] = static_cast<std::uint8_t>(0x40U + i);
        }
        std::byte block[16] = {};
        for (std::size_t i = 0U; i < 16U; ++i) {
            block[i] = static_cast<std::byte>(bytes[i]);
        }
        const auto written = access.write(0U, std::span<const std::byte>(block),
                                          AccessChannel::PageCache, params);
        expect(written.ok() && written.value == 16U, "a block writes and reports sixteen bytes");
        expect(window.blocks == 1 && window.position == 0x1000U,
               "once, at the position the parameters gave");
        expect(window.last[0] == 0x40U && window.last[15] == 0x4FU,
               "with all sixteen bytes carried through");
        const std::byte word[8] = {};
        expect(access.write(0U, std::span<const std::byte>(word), AccessChannel::PageCache, params).status
                   == Access::AccessStatus::BadWidth,
               "a word is refused by width: this route has no word to give");
        expect(access.read(0U, 16U, AccessChannel::PageCache, params).status
                   == Access::AccessStatus::Unsupported,
               "and a read says unsupported rather than returning zeros as data");
        expect(access.shutdown().ok() && window.closed == 1, "and the route comes back down");
    }

    void without_credentials_or_with_the_wrong_set_nothing_comes_up() {
        RecordingWindow window;
        PageCacheSeam seam(window);
        Access access(seam);
        AccessParams empty;
        empty.offset_set = true;
        expect(!access.initialize(empty).ok(), "a set with no credentials does not bring it up");
        expect(window.opened == 0, "and the doors stay shut");
        const ForeignParams foreign;
        expect(access.initialize(foreign).status == Access::AccessStatus::BadParameters,
               "and a set belonging to the other backend is refused by name");
    }
}  // namespace

int main() {
    std::printf("page cache chain: one block, end to end\n");
    the_chain_writes_one_block();
    std::printf("page cache chain: what does not come up\n");
    without_credentials_or_with_the_wrong_set_nothing_comes_up();
    if (g_failures != 0U) {
        std::printf("page cache chain: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("page cache chain: PASS (credentials, one block, width refused)\n");
    return 0;
}
