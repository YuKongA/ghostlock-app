#pragma once
/** @file jni/marshal_43284.hpp
 *  43284 的结果数组布局（**不含 JNI**）：名字表与填充。
 *
 *  请求参数的权威是 D1 单表（attack_contract_values.hpp）与其读取器；这里只剩 snapshot 的六个值。
 */
#include <cstddef>
#include <cstdint>

#include "jni/api_43284.hpp"
#include "jni/field_layout.hpp"

namespace ghostlock::jni::marshal::cve43284 {
    /** snapshot 的结果数组布局。 */
    struct Snapshot43284Layout final {
        static constexpr const char *const names[] = {"phase", "flags", "width", "ticket", "role",
                                                      "sequence"};
        static constexpr std::size_t count = sizeof(names) / sizeof(names[0]);

        [[nodiscard]] static constexpr std::size_t index(const std::string_view name) noexcept {
            return field_index(names, count, name);
        }
    };

    /** 把快照写进值数组（长度必须 ≥ count）。 */
    inline void fill_snapshot(std::int64_t *out, const Snapshot43284 &snapshot) noexcept {
        out[Snapshot43284Layout::index("phase")] = static_cast<std::int64_t>(snapshot.phase);
        out[Snapshot43284Layout::index("flags")] = static_cast<std::int64_t>(snapshot.flags);
        out[Snapshot43284Layout::index("width")] = static_cast<std::int64_t>(snapshot.width);
        out[Snapshot43284Layout::index("ticket")] = static_cast<std::int64_t>(snapshot.ticket);
        out[Snapshot43284Layout::index("role")] = static_cast<std::int64_t>(snapshot.role);
        out[Snapshot43284Layout::index("sequence")] = static_cast<std::int64_t>(snapshot.sequence);
    }
}  // namespace ghostlock::jni::marshal::cve43284
