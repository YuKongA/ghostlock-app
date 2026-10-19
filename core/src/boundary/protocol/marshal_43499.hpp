#pragma once
/** @file boundary/protocol/marshal_43499.hpp
 *  43499 的 JNI 编组（**不含 JNI**）：**结果**数组的名字表与填充。
 *
 *  请求参数类的唯一权威是 D1 单表（attack_contract_values.hpp）与其读取器；这里只剩 snapshot 的
 *  结果数组：每一位是什么由名字表决定，填充按名字取下标，JNI 外壳只负责取数组与写回数组。
 */
#include <cstddef>
#include <cstdint>

#include "boundary/jni/api_43499.hpp"
#include "boundary/protocol/field_layout.hpp"

namespace ghostlock::jni::marshal::cve43499 {
    /** snapshot 的结果数组布局。 */
    struct Snapshot43499Layout final {
        static constexpr const char *const names[] = {
            "phase",       "flags",        "width",     "page_base",  "word0",
            "word1",       "payload_page", "payload_base", "alias_delta", "found",
            "mm_struct",   "victim_task",  "victim_pid",
        };
        static constexpr std::size_t count = sizeof(names) / sizeof(names[0]);

        [[nodiscard]] static constexpr std::size_t index(const std::string_view name) noexcept {
            return field_index(names, count, name);
        }
    };

    /** 把快照写进值数组（长度必须 ≥ Snapshot43499Layout::count）。 */
    inline void fill_snapshot(std::int64_t *out, const Snapshot43499 &snapshot) noexcept {
        out[Snapshot43499Layout::index("phase")] = static_cast<std::int64_t>(snapshot.phase);
        out[Snapshot43499Layout::index("flags")] = static_cast<std::int64_t>(snapshot.flags);
        out[Snapshot43499Layout::index("width")] = static_cast<std::int64_t>(snapshot.width);
        out[Snapshot43499Layout::index("page_base")] =
            static_cast<std::int64_t>(snapshot.facts.page_base);
        out[Snapshot43499Layout::index("word0")] = static_cast<std::int64_t>(snapshot.facts.word0);
        out[Snapshot43499Layout::index("word1")] = static_cast<std::int64_t>(snapshot.facts.word1);
        out[Snapshot43499Layout::index("payload_page")] =
            static_cast<std::int64_t>(snapshot.facts.payload_page);
        out[Snapshot43499Layout::index("payload_base")] =
            static_cast<std::int64_t>(snapshot.facts.payload_base);
        out[Snapshot43499Layout::index("alias_delta")] =
            static_cast<std::int64_t>(snapshot.facts.alias_delta);
        out[Snapshot43499Layout::index("found")] = static_cast<std::int64_t>(snapshot.facts.found);
        out[Snapshot43499Layout::index("mm_struct")] =
            static_cast<std::int64_t>(snapshot.facts.mm_struct);
        out[Snapshot43499Layout::index("victim_task")] =
            static_cast<std::int64_t>(snapshot.facts.victim_task);
        out[Snapshot43499Layout::index("victim_pid")] =
            static_cast<std::int64_t>(snapshot.facts.victim_pid);
    }

}  // namespace ghostlock::jni::marshal::cve43499
