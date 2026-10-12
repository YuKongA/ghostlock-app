/** @file tests/pagecache_chain_test.cpp
 *  The second backend's pieces wired into one, driven end to end.
 *
 *  What is checked is that they agree: the access comes up only with credentials, takes them from
 *  its own parameter set and refuses a set belonging to the other backend; the write goes down
 *  through the seam to the window as a sixteen-byte block at the position the parameters gave; the
 *  read comes back through the same window at that same position, with a word read served as the
 *  block's first eight bytes and a narrower block refused by width; and the route comes back down.
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
        int reads = 0;
        std::uint64_t position = 0U;
        std::uint64_t read_position = 0U;
        std::uint8_t last[16] = {};
        std::uint8_t first_byte_of_secrets = 0U;
        /** What the carrier would answer with, whether this window has a read face at all, and
         *  whether a read that does exist completes. */
        std::uint8_t source[16] = {};
        bool can_read_flag = true;
        bool read_ok = true;

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
        [[nodiscard]] bool can_read() const noexcept override { return can_read_flag; }
        /** The sequence the access handed down, so the chain can pin that it reaches the window. */
        std::uint32_t adopted_sequence = 0U;
        void adopt_sequence(std::uint32_t next) noexcept override { adopted_sequence = next; }
        [[nodiscard]] bool read_block(std::uint64_t at, std::uint8_t out[16]) noexcept override {
            ++reads;
            read_position = at;
            if (!read_ok) {
                return false;
            }
            for (std::size_t i = 0U; i < 16U; ++i) {
                out[i] = source[i];
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
        Access access(&seam);
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
        /* The read face, which is what a verification reads: the block comes back from the same
         *  window, at the position the write used, so a verification cannot disagree with the write
         *  about where it is looking. */
        for (std::size_t i = 0U; i < 16U; ++i) {
            window.source[i] = static_cast<std::uint8_t>(0x80U + i);
        }
        const auto read_back = access.read(0U, 16U, AccessChannel::PageCache, params);
        expect(read_back.ok() && read_back.value.size() == 16U,
               "a block reads back and reports sixteen bytes");
        expect(window.reads == 1 && window.read_position == 0x1000U,
               "read at the position the parameters carry, which is the one the write used");
        if (read_back.ok() && read_back.value.size() == 16U) {
            expect(std::to_integer<unsigned>(read_back.value[0]) == 0x80U &&
                       std::to_integer<unsigned>(read_back.value[15]) == 0x8FU,
                   "and the carrier's bytes are the answer, not zeros");
        }
        const auto word_read = access.read64(0U, AccessChannel::PageCache, params);
        expect(word_read.ok() && word_read.value == 0x8786858483828180ULL,
               "a word read is that same block, first eight bytes little-endian");
        expect(access.read(0U, 8U, AccessChannel::PageCache, params).status ==
                   Access::AccessStatus::BadWidth,
               "a narrower read is refused by width rather than served a truncated block");
        expect(window.reads == 2, "so no extra block was read for either of those");
        window.read_ok = false;
        expect(access.read(0U, 16U, AccessChannel::PageCache, params).status ==
                   Access::AccessStatus::Failed,
               "a block that cannot be read is refused as a call that failed");
        window.read_ok = true;
        expect(access.shutdown().ok() && window.closed == 1, "and the route comes back down");
    }

    /** A caller that has already written (patch #1, another carrier, the hook) hands the session's
     *  sequence in; bring-up must adopt it after opening the doors, because opening them starts a
     *  fresh sequence -- the device dropped every datagram of a window that restarted at one. */
    void a_supplied_sequence_reaches_the_window() {
        RecordingWindow window;
        PageCacheSeam seam(window);
        Access access(&seam);
        AccessParams params;
        params.sa = credentials();
        params.offset = 0x2000U;
        params.offset_set = true;
        params.sequence = 91U;
        params.sequence_set = true;
        expect(access.initialize(params).ok(), "the route comes up with a sequence supplied");
        expect(window.adopted_sequence == 91U, "and the window continues at the sequence given");
        RecordingWindow plain;
        PageCacheSeam plain_seam(plain);
        Access plain_access(&plain_seam);
        AccessParams none;
        none.sa = credentials();
        none.offset = 0x2000U;
        none.offset_set = true;
        expect(plain_access.initialize(none).ok(), "a set with no sequence still comes up");
        expect(plain.adopted_sequence == 0U, "and its window is left to start its own");
    }

    /** A read before the route is up, and a route with no read face: two different answers. */
    void reads_are_refused_by_name_before_they_can_work() {
        RecordingWindow window;
        PageCacheSeam seam(window);
        Access access(&seam);
        AccessParams params;
        params.sa = credentials();
        params.offset = 0x2000U;
        params.offset_set = true;
        expect(access.read(0U, 16U, AccessChannel::PageCache, params).status ==
                   Access::AccessStatus::NotInitialized,
               "a read before initialize is refused as not initialized");
        expect(window.reads == 0, "and the window was not asked");
        RecordingWindow window_without_read;
        window_without_read.can_read_flag = false;
        PageCacheSeam without_read(window_without_read);
        Access readless(&without_read);
        expect(readless.initialize(params).ok(), "a route without a read face still comes up");
        expect(readless.read(0U, 16U, AccessChannel::PageCache, params).status ==
                   Access::AccessStatus::Unsupported,
               "and its read is refused as a capability this route does not have");
        expect(window_without_read.reads == 0,
               "the window that refused the capability was never asked for a block");
        expect(access.read(0U, 16U, AccessChannel::PageCache, params).status ==
                   Access::AccessStatus::NotInitialized,
               "while the first access is still not initialized, which is its own answer");
    }

    void without_credentials_or_with_the_wrong_set_nothing_comes_up() {
        RecordingWindow window;
        PageCacheSeam seam(window);
        Access access(&seam);
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
    std::printf("page cache chain: the sequence a caller hands in\n");
    a_supplied_sequence_reaches_the_window();
    std::printf("page cache chain: reads that are refused by name\n");
    reads_are_refused_by_name_before_they_can_work();
    std::printf("page cache chain: what does not come up\n");
    without_credentials_or_with_the_wrong_set_nothing_comes_up();
    if (g_failures != 0U) {
        std::printf("page cache chain: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("page cache chain: PASS (credentials, block write, block read, width refused)\n");
    return 0;
}
