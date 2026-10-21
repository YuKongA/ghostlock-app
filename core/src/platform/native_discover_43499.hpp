#pragma once
/** Kernelsnitch discover 的 native 端口：调用方给 words mapping，C++ 只跑 run_discovery。 */
#include <cstddef>
#include <cstdint>
#include <string>

namespace ghostlock::platform::native43499 {
    struct DiscoverParams final {
        std::uint64_t words_address = 0U;
        std::uint32_t words_bytes = 0U;
        std::uint32_t collisions = 0U;
        std::uint32_t thread_count = 0U;
        std::uint32_t possible_cpus = 0U;
        std::uint64_t page_size = 0U;
        std::uint64_t mm_struct_size = 0U;
        std::uint64_t mm_slab_order = 0U;
        std::uint32_t header_bytes = 0U;
        std::uint32_t total_pokes = 0U;
        std::uint32_t word_bytes = 0U;
        std::uint32_t handle_bytes = 0U;
        std::uint32_t wake_rounds = 0U;
        std::int64_t round_timeout_us = 0;
        std::uint64_t range_start = 0U;
        std::uint64_t range_end = 0U;
        std::uint32_t counter_offset = 0U;
        bool sweep_tags = false;
    };

    struct DiscoverResult final {
        bool ran = false;
        std::uint8_t verdict = 0U;
        std::uint8_t refusal = 0U;
        std::uint64_t found = 0U;
        std::uint64_t mm_struct = 0U;
        std::uint32_t probes = 0U;
        std::uint32_t rounds = 0U;
        std::uint32_t pokes = 0U;
        std::int32_t error = 0;
        std::uint64_t words_base = 0U;
        std::uint32_t words_bytes = 0U;
    };

    [[nodiscard]] bool parse_discover(const std::uint8_t *blob, std::size_t size,
                                      DiscoverParams &out, std::string &error) noexcept;
    [[nodiscard]] long run_discover(const DiscoverParams &params, DiscoverResult &out) noexcept;
    [[nodiscard]] bool encode_discover_result(const DiscoverResult &result, std::uint8_t *out,
                                              std::size_t capacity) noexcept;
}  // namespace ghostlock::platform::native43499
