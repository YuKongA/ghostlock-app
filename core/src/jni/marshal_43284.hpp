#pragma once
/** @file jni/marshal_43284.hpp
 *  43284 的 JNI 编组（**不含 JNI**）：名字表 + 值数组的填充/解析。
 *
 *  与 43499 那份同构：名字表是唯一权威，填充与解析都按名字取下标。
 */
#include <cstddef>
#include <cstdint>

#include "jni/api_43284.hpp"
#include "jni/field_layout.hpp"

namespace ghostlock::jni::marshal::cve43284 {
    /** snapshot 的结果数组布局。 */
    struct Snapshot43284Layout final {
        static constexpr const char *const names[] = {"phase", "flags", "width", "token", "role",
                                                      "sequence"};
        static constexpr std::size_t count = sizeof(names) / sizeof(names[0]);

        [[nodiscard]] static constexpr std::size_t index(const std::string_view name) noexcept {
            return field_index(names, count, name);
        }
    };

    /** initialize 的参数数组布局。 */
    struct Initialize43284Layout final {
        static constexpr const char *const names[] = {
            "trigger_delay_ms",
            "wait_budget_ms",
            "wait_poll_ms",
            "carrier_base",
        };
        static constexpr std::size_t count = sizeof(names) / sizeof(names[0]);

        [[nodiscard]] static constexpr std::size_t index(const std::string_view name) noexcept {
            return field_index(names, count, name);
        }
    };

    /** hook_words 的结果数组布局。 */
    struct Hook43284Layout final {
        static constexpr const char *const names[] = {
            "branch_word",
            "displaced_word",
            "site_offset",
            "landing_offset",
        };
        static constexpr std::size_t count = sizeof(names) / sizeof(names[0]);

        [[nodiscard]] static constexpr std::size_t index(const std::string_view name) noexcept {
            return field_index(names, count, name);
        }
    };

    /** 把快照写进值数组（长度必须 ≥ Snapshot43284Layout::count）。 */
    inline void fill_snapshot(std::int64_t *out, const Snapshot43284 &snapshot) noexcept {
        out[Snapshot43284Layout::index("phase")] = static_cast<std::int64_t>(snapshot.phase);
        out[Snapshot43284Layout::index("flags")] = static_cast<std::int64_t>(snapshot.flags);
        out[Snapshot43284Layout::index("width")] = static_cast<std::int64_t>(snapshot.width);
        out[Snapshot43284Layout::index("token")] = static_cast<std::int64_t>(snapshot.facts.token);
        out[Snapshot43284Layout::index("role")] = static_cast<std::int64_t>(snapshot.facts.role);
        out[Snapshot43284Layout::index("sequence")] = static_cast<std::int64_t>(snapshot.facts.sequence);
    }

    /** 从值数组解析初始化参数（长度必须 ≥ Initialize43284Layout::count）。 */
    [[nodiscard]] inline Initialize43284 parse_initialize(const std::int64_t *values) noexcept {
        Initialize43284 parsed{};
        parsed.trigger_delay_ms =
            static_cast<std::uint64_t>(values[Initialize43284Layout::index("trigger_delay_ms")]);
        parsed.wait_budget_ms =
            static_cast<std::uint64_t>(values[Initialize43284Layout::index("wait_budget_ms")]);
        parsed.wait_poll_ms =
            static_cast<std::uint64_t>(values[Initialize43284Layout::index("wait_poll_ms")]);
        parsed.carrier_base =
            static_cast<std::uint64_t>(values[Initialize43284Layout::index("carrier_base")]);
        return parsed;
    }

    /** 把钩子的四个数字写进值数组（长度必须 ≥ Hook43284Layout::count）。 */
    inline void fill_hook(std::int64_t *out, const HookWords43284 &words) noexcept {
        out[Hook43284Layout::index("branch_word")] = static_cast<std::int64_t>(words.branch_word);
        out[Hook43284Layout::index("displaced_word")] =
            static_cast<std::int64_t>(words.displaced_word);
        out[Hook43284Layout::index("site_offset")] = static_cast<std::int64_t>(words.site_offset);
        out[Hook43284Layout::index("landing_offset")] =
            static_cast<std::int64_t>(words.landing_offset);
    }
}  // namespace ghostlock::jni::marshal::cve43284
