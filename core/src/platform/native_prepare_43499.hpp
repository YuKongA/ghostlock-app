#pragma once
/** Native prepare 端口：handle 持有 WordsRegion；C++ 只调 run_prepare。 */
#include <cstddef>
#include <cstdint>
#include <string>

namespace ghostlock::platform::native43499 {
    struct PrepareParams final {
        std::uint64_t page_size = 0U;
        std::uint64_t mm_struct_size = 0U;
        std::uint64_t mm_slab_order = 0U;
        std::uint32_t collisions = 0U;
        std::uint32_t thread_count = 0U;
        std::uint32_t possible_cpus = 0U;
        std::uint32_t proof_count = 0U;
        std::uint64_t proof_seed = 0U;
        std::uint64_t range_start = 0U;
        std::uint64_t range_end = 0U;
        std::uint32_t counter_offset = 0U;
        std::uint32_t wake_rounds = 0U;
        std::int64_t round_timeout_us = 0;
        std::uint32_t header_bytes = 0U;
        std::uint32_t total_pokes = 0U;
        std::uint32_t word_bytes = 0U;
        std::uint32_t handle_bytes = 0U;
        std::uint32_t pre_groups = 0U;
        std::uint32_t pre_shaped_groups = 0U;
        std::uint32_t spray_groups = 0U;
        std::uint32_t post_groups = 0U;
        std::uint32_t leak_groups = 0U;
        std::uint32_t snitch_collisions = 0U;
        std::uint32_t max_attempts = 0U;
        std::uint32_t poll_rounds = 0U;
        std::int32_t socket_buffer = 0;
        std::int32_t main_cpu = 0;
        bool sweep_tags = false;
    };

    struct PrepareResult final {
        bool ran = false;
        std::uint8_t status = 0U;
        std::uint8_t refusal = 0U;
        std::uint8_t at_step = 0U;
        std::uint64_t base = 0U;
        std::uint64_t words_base = 0U;
        std::uint64_t payload_base = 0U;
        std::int32_t shaping_fd = -1;
        std::int32_t shaping_peer_fd = -1;
        std::uint32_t opened = 0U;
        std::uint32_t closed = 0U;
        std::int32_t error = 0;
        std::uint32_t proof_count = 0U;
        std::uint64_t snitch_found = 0U;
        std::uint64_t snitch_address = 0U;
        std::uint8_t snitch_verdict = 0U;
        std::uint64_t proofs[4] = {};
        std::uint64_t words[4] = {};
        std::uint32_t payload_offset = 0U;
    };

    struct PrepareHandle;

    [[nodiscard]] PrepareHandle *prepare_create() noexcept;
    void prepare_destroy(PrepareHandle *handle) noexcept;
    [[nodiscard]] bool parse_prepare(const std::uint8_t *blob, std::size_t size,
                                     PrepareParams &out, std::string &error) noexcept;
    [[nodiscard]] long prepare_run(PrepareHandle *handle, const PrepareParams &params,
                                   PrepareResult &out) noexcept;
    [[nodiscard]] bool prepare_word(const PrepareHandle *handle, std::uint32_t index,
                                    std::uint64_t &out) noexcept;
    [[nodiscard]] std::uint64_t prepare_words_base(const PrepareHandle *handle) noexcept;
    [[nodiscard]] std::uint64_t prepare_payload_base(const PrepareHandle *handle) noexcept;
    [[nodiscard]] bool encode_prepare_result(const PrepareResult &result, std::uint8_t *out,
                                             std::size_t capacity) noexcept;
}  // namespace ghostlock::platform::native43499
