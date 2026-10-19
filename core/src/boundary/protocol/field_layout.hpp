#pragma once
/** @file boundary/protocol/field_layout.hpp
 *  数组字段布局的**单一权威**：名字表 + 由名字取下标。
 *
 *  为什么需要它：JNI 没有结构体，跨端传的就是一串数字。审查指出"自描述"如果只是另写一张名字表，
 *  那它和 values[i] 的映射仍然是两份手写的东西，迟早对不上。这里让两边都只认名字：
 *  名字表是唯一的一份，下标由名字算出来（编译期），填充代码写的是 values[index("page_base")]，
 *  于是"第几个是什么"不可能在别处再抄一遍。
 *
 *  本文件不依赖 JNI：宿主测试可以直接跑名字表与索引的唯一性、范围、以及往返值。
 */
#include <cstddef>
#include <string_view>

namespace ghostlock::jni {
    /** @return name 在名字表里的下标，找不到时返回 count。 */
    [[nodiscard]] constexpr std::size_t field_index(const char *const *names, const std::size_t count,
                                                    const std::string_view name) noexcept {
        for (std::size_t index = 0U; index < count; ++index) {
            const char *cursor = names[index];
            std::size_t at = 0U;
            while (cursor[at] != '\0' && at < name.size() && cursor[at] == name[at]) {
                ++at;
            }
            if (cursor[at] == '\0' && at == name.size()) {
                return index;
            }
        }
        return count;
    }

    /** @return 名字表里有没有重复（有重复就是两份权威，测试会拦下来）。 */
    [[nodiscard]] constexpr bool field_names_unique(const char *const *names,
                                                    const std::size_t count) noexcept {
        for (std::size_t left = 0U; left < count; ++left) {
            for (std::size_t right = left + 1U; right < count; ++right) {
                if (field_index(names, count, std::string_view(names[left])) == right) {
                    return false;
                }
            }
        }
        return true;
    }
}  // namespace ghostlock::jni
