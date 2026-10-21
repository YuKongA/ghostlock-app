#pragma once
/** N2 的 43499 C++ actor batch：参数解析、三线程执行、结果编码。 */
#include <cstddef>
#include <cstdint>
#include <string>

namespace ghostlock::platform::native43499 {
    struct AttemptParams final {
        std::uint64_t chain_address = 0U;
        std::uint64_t wait_address = 0U;
        std::uint64_t target_address = 0U;
        std::uint64_t page_sink_address = 0U;
        std::uint32_t chain_value = 0U;
        std::uint32_t wait_value = 0U;
        std::uint32_t target_value = 0U;
        const std::uint8_t *page = nullptr;
        std::size_t page_length = 0U;
        std::uint8_t pointer_bytes = 0U;
        std::int64_t park_timeout_ns = -1;
        std::uint32_t join_timeout_ms = 0U;
        std::uint32_t settle_us = 0U;
        std::uint16_t waiter_cpu = 0U;
        std::uint16_t owner_cpu = 0U;
        std::uint16_t requeue_cpu = 0U;
        std::int32_t waiter_nice = 0;
        std::int32_t owner_nice = 0;
        std::int32_t requeue_nice = 0;
        std::uint32_t policy = 0U;
        /** >=0: 把 page 写进该 fd（shaping socket）；-1: 写 page_sink_address。 */
        std::int32_t page_fd = -1;
    };

    struct AttemptResult final {
        std::int32_t waiter_result = 0;
        std::int32_t wait_result = 0;
        std::int32_t owner_result = 0;
        std::int32_t requeue_result = 0;
        bool actors_started = false;
        bool join_timed_out = false;
        std::uint8_t page_stage = 0U;
        std::uint32_t page_errno = 0U;
        std::uint32_t page_bytes_written = 0U;
        std::uint64_t elapsed_ns = 0U;
    };

    [[nodiscard]] bool parse_attempt(const std::uint8_t *blob, std::size_t size,
                                     AttemptParams &out, std::string &error) noexcept;
    [[nodiscard]] bool encode_result(const AttemptResult &result, std::uint8_t *out,
                                     std::size_t capacity) noexcept;
    /** @return 0 或负 errno；结果写入 out。 */
    [[nodiscard]] long run_attempt(const AttemptParams &params, AttemptResult &out) noexcept;
}  // namespace ghostlock::platform::native43499
