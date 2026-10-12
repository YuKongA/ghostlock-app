#pragma once
/** @file backend/cve_2026_43499/steps/prepare_mapping.hpp
 *  准备阶段的**映射**：wire（schema 键）→ 各单元的输入结构。
 *
 *  为什么单独一件：映射是「数据放在哪」的唯一定义处；单元本身只认自己的结构体。表驱动——
 *  每个输入一条 {名称, schema 键}，填不进去的时候拒绝名里带的是**哪个**输入。
 *
 *  缺失语义：值为 0 或没有文档键 ⇒ 具名拒绝（`prepare-value-missing:<输入名>`），不许填零。
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43499/leak/snitch_setup.hpp"
#include "backend/cve_2026_43499/leak/snitch_scan.hpp"
#include "backend/cve_2026_43499/leak/scan_geometry.hpp"
#include "backend/cve_2026_43499/leak/snitch_region.hpp"
#include "backend/cve_2026_43499/memory/multicast_arm.hpp"
#include "backend/cve_2026_43499/memory/prepare_plan.hpp"
#include "backend/cve_2026_43499/memory/reclaim_plan.hpp"
#include "backend/cve_2026_43499/memory/shaping_plan.hpp"

namespace ghostlock::backend::cve_2026_43499::steps {
    /** One preparation input, the wire key it comes from, and the name a missing value gets.
     *  The refusal is a constant per input: nothing is formatted at run time, so no buffer and no
     *  mutable state (AGENTS 4). */
    struct PrepareInputKey final {
        const char *input;      /**< 单元结构体里的字段名（拒绝信息里用的就是它） */
        /** The schema key it comes from; nullptr means the schema does not carry it yet --
         *  the document cannot supply it, so the step refuses by name instead of inventing one. */
        const char *wire_key;
        const char *refusal;    /**< 缺值时的具名拒绝（常量） */
    };

    /** The table: every preparation input and where it lives in the wire. */
    inline constexpr PrepareInputKey kPrepareInputKeys[] = {
        {"snitch_collisions", "backend.cve_2026_43499.execution.heap.kernelsnitch_collisions", "prepare-value-missing:snitch_collisions"},
        {"snitch_threads", "backend.cve_2026_43499.execution.heap.snitch_threads", "prepare-value-missing:snitch_threads"},
        {"futex_table_size", "backend.cve_2026_43499.execution.heap.futex_table_size", "prepare-value-missing:futex_table_size"},
        {"mm_struct_sz", "backend.cve_2026_43499.execution.heap.mm_struct_sz", "prepare-value-missing:mm_struct_sz"},
        {"region_threads", "backend.cve_2026_43499.execution.heap.region_threads", "prepare-value-missing:region_threads"},
        {"main_cpu", "backend.cve_2026_43499.execution.heap.main_cpu", "prepare-value-missing:main_cpu"},
        {"poll_rounds", "backend.cve_2026_43499.execution.heap.poll_rounds", "prepare-value-missing:poll_rounds"},
        {"snitch_round_wait_us", "backend.cve_2026_43499.execution.heap.snitch_round_wait_us", "prepare-value-missing:snitch_round_wait_us"},
        {"snitch_wake_rounds", "backend.cve_2026_43499.execution.heap.snitch_wake_rounds", "prepare-value-missing:snitch_wake_rounds"},
        {"prepare_max_attempts", "backend.cve_2026_43499.execution.heap.prepare_max_attempts", "prepare-value-missing:prepare_max_attempts"},
        {"prepare_timeout_ms", "backend.cve_2026_43499.execution.heap.prepare_timeout_ms", "prepare-value-missing:prepare_timeout_ms"},
        {"kernelsnitch_timeout_ms", "backend.cve_2026_43499.execution.heap.kernelsnitch_timeout_ms", "prepare-value-missing:kernelsnitch_timeout_ms"},
        {"setup_settle_us", "backend.cve_2026_43499.execution.race.setup_settle_us", "prepare-value-missing:setup_settle_us"},
        {"state_poll_interval_us", "backend.cve_2026_43499.execution.race.state_poll_interval_us", "prepare-value-missing:state_poll_interval_us"},
        {"route_wait_ms", "backend.cve_2026_43499.execution.race.route_wait_ms", "prepare-value-missing:route_wait_ms"},
        {"route_done_timeout_ms", "backend.cve_2026_43499.execution.race.route_done_timeout_ms", "prepare-value-missing:route_done_timeout_ms"},
        {"waiter_offset", "backend.cve_2026_43499.route.multicast_waiter.abi.waiter_off", "prepare-value-missing:waiter_offset"},
        {"task_offset", "backend.cve_2026_43499.route.multicast_waiter.abi.task_offset", "prepare-value-missing:task_offset"},
        {"lock_offset", "backend.cve_2026_43499.route.multicast_waiter.abi.lock_offset", "prepare-value-missing:lock_offset"},
        {"buffer_size", "backend.cve_2026_43499.route.multicast_waiter.abi.buffer_size", "prepare-value-missing:buffer_size"},
        {"word_bytes", "backend.cve_2026_43499.execution.heap.word_bytes", "prepare-value-missing:word_bytes"},
        {"handle_bytes", "backend.cve_2026_43499.execution.heap.handle_bytes", "prepare-value-missing:handle_bytes"},
        {"coarse_stride", "backend.cve_2026_43499.execution.heap.coarse_stride", "prepare-value-missing:coarse_stride"},
        {"slab_stride", "backend.cve_2026_43499.execution.heap.slab_stride", "prepare-value-missing:slab_stride"},
        {"mm_objs_per_slab", "backend.cve_2026_43499.execution.heap.mm_objs_per_slab", "prepare-value-missing:mm_objs_per_slab"},
        {"shaped", "backend.cve_2026_43499.execution.heap.shaped", "prepare-value-missing:shaped"},
        {"socket_buffer", "backend.cve_2026_43499.execution.heap.socket_buffer", "prepare-value-missing:socket_buffer"},
        {"send_size", "backend.cve_2026_43499.execution.heap.send_size", "prepare-value-missing:send_size"},
        /* 区域/几何那一组：单元要，但 schema 现在没有这些键 ⇒ 文档供不上，本步 refuse 而不编造。 */
        {"header_bytes", "backend.cve_2026_43499.execution.heap.header_bytes", "prepare-value-missing:header_bytes"},
        {"total_pokes", "backend.cve_2026_43499.execution.heap.total_pokes", "prepare-value-missing:total_pokes"},
        {"page_size", "backend.cve_2026_43499.execution.heap.page_size", "prepare-value-missing:page_size"},
        {"region_size", "backend.cve_2026_43499.execution.heap.region_size", "prepare-value-missing:region_size"},
        {"object_size", "backend.cve_2026_43499.execution.heap.object_size", "prepare-value-missing:object_size"},
        {"pre_groups", "backend.cve_2026_43499.execution.heap.pre_groups", "prepare-value-missing:pre_groups"},
        {"spray_groups", "backend.cve_2026_43499.execution.heap.spray_groups", "prepare-value-missing:spray_groups"},
        {"post_groups", "backend.cve_2026_43499.execution.heap.post_groups", "prepare-value-missing:post_groups"},
        {"leak_groups", "backend.cve_2026_43499.execution.heap.leak_groups", "prepare-value-missing:leak_groups"},
    };
    inline constexpr std::size_t kPrepareInputKeyCount =
            sizeof(kPrepareInputKeys) / sizeof(kPrepareInputKeys[0]);

    /** The values, already read out of the wire by the caller (all zero means "not read"). */
    struct PrepareValues final {
        std::uint64_t snitch_collisions = 0U;
        std::uint64_t snitch_threads = 0U;
        std::uint64_t futex_table_size = 0U;
        std::uint64_t snitch_round_wait_us = 0U;
        std::uint64_t snitch_wake_rounds = 0U;
        std::uint64_t prepare_max_attempts = 0U;
        std::uint64_t prepare_timeout_ms = 0U;
        std::uint64_t kernelsnitch_timeout_ms = 0U;
        std::uint64_t setup_settle_us = 0U;
        std::uint64_t state_poll_interval_us = 0U;
        std::uint64_t route_wait_ms = 0U;
        std::uint64_t route_done_timeout_ms = 0U;
        std::uint64_t waiter_offset = 0U;
        std::uint64_t task_offset = 0U;
        std::uint64_t lock_offset = 0U;
        std::uint64_t buffer_size = 0U;
        /* 下面这几个 schema 还没有键：留着字段，是为了让「缺」这件事有形。 */
        std::uint64_t header_bytes = 0U;
        std::uint64_t total_pokes = 0U;
        std::uint64_t page_size = 0U;
        std::uint64_t region_size = 0U;
        std::uint64_t object_size = 0U;
        std::uint64_t mm_struct_sz = 0U;
        std::uint64_t region_threads = 0U;
        std::uint64_t main_cpu = 0U;
        std::uint64_t poll_rounds = 0U;
        std::uint64_t pre_groups = 0U;
        std::uint64_t spray_groups = 0U;
        std::uint64_t post_groups = 0U;
        std::uint64_t leak_groups = 0U;
        std::uint64_t word_bytes = 0U;
        std::uint64_t handle_bytes = 0U;
        std::uint64_t coarse_stride = 0U;
        std::uint64_t slab_stride = 0U;
        std::uint64_t mm_objs_per_slab = 0U;
        std::uint64_t shaped = 0U;
        std::uint64_t socket_buffer = 0U;
        std::uint64_t send_size = 0U;
    };

    /** What the mapping produced. */
    struct PrepareMapped final {
        leak::SnitchSetup snitch{};
        memory::MulticastGeometry geometry{};
        std::uint64_t prepare_max_attempts = 0U;
        std::uint64_t prepare_timeout_ms = 0U;
        std::uint64_t route_wait_ms = 0U;
        std::uint64_t route_done_timeout_ms = 0U;
        std::uint64_t setup_settle_us = 0U;
        std::uint64_t state_poll_interval_us = 0U;
    };

    /** Fill the units" inputs from the wire"s values.
     *  @param values the values the caller read (zero means the document did not carry it)
     *  @param out    receives the mapped inputs
     *  @param refusal receives `prepare-value-missing:<input>` (the input"s name, from the table)
     *  @return true when every input had a value. */
    [[nodiscard]] bool map_prepare_inputs(const PrepareValues &values, PrepareMapped &out,
                                          const char **refusal) noexcept;

    /** The region and the patience the search is given, filled from the same values. */
    struct RegionMapped final {
        leak::RegionInputs inputs{};
        leak::SnitchPatience patience{};
        leak::ScanStrides strides{};
    };

    /** Fill the region inputs (seven widths and counts) and the patience from the values.
     *  @return true, or false with the same named refusals map_prepare_inputs uses. */
    [[nodiscard]] bool map_region_inputs(const PrepareValues &values, RegionMapped &out,
                                          const char **refusal) noexcept;

    /** The reclaim counts and the shaping inputs, from the same values. */
    struct ReclaimMapped final {
        memory::ReclaimCounts counts{};
        memory::ShapingInputs shaping{};
    };

    /** Fill the reclaim counts and the shaping inputs. The shaping plan itself is built by the
     *  unit (its own verdict names come back through @p refusal as prepare-shaping:<verdict>).
     *  @return true, or false with a named reason. */
    [[nodiscard]] bool map_reclaim_inputs(const PrepareValues &values, ReclaimMapped &out,
                                          const char **refusal) noexcept;
    /** Everything the preparation needs, mapped in one call. */
    struct AllMapped final {
        PrepareMapped base{};
        RegionMapped region{};
        ReclaimMapped reclaim{};
    };

    /** Map the values in the order the plan runs the segments: base, then the region (the search
     *  sizes and the strides), then the reclaim and the shaping. The first missing value is the one
     *  named, so the caller learns which segment stopped it.
     *  @return true when every segment mapped, or false with a named reason. */
    [[nodiscard]] bool map_all_prepare(const PrepareValues &values, AllMapped &out,
                                       const char **refusal) noexcept;
}  // namespace ghostlock::backend::cve_2026_43499::steps
