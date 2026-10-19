#pragma once
/** @file contract/attack_contract_model.hpp
 *  D1 字段表的无 JNI 投影：生成器与宿主测试共同消费它。
 */

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ghostlock::jni::contract {
    /** JNI 可直接、无歧义读取的字段类型闭集。 */
    enum class FieldKind : std::uint8_t {
        Long,
        Int,
        Boolean,
        Bytes,
        Bytes16,
        Bytes32,
        Secret32,
    };

    /** 单字段的跨端定义。 */
    struct Field final {
        const char *group;
        const char *name;
        FieldKind kind;
        const char *jni_signature;
        std::size_t fixed_bytes;
        std::size_t allocation_max_bytes;
    };

    /** 单参数对象的声明；字段由紧随其后的同名条目组成。 */
    struct Group final {
        const char *name;
    };

    inline constexpr Group kGroups[] = {
#define GL_ATTACK_GROUP(group_name) {#group_name},
#define GL_ATTACK_FIELD(group_name, field_name, field_kind)
#define GL_ATTACK_BYTES_FIELD(group_name, field_name, max_bytes)
#define GL_ATTACK_END_GROUP(group_name)
#include "contract/attack_contract_fields.hpp"
#undef GL_ATTACK_END_GROUP
#undef GL_ATTACK_BYTES_FIELD
#undef GL_ATTACK_FIELD
#undef GL_ATTACK_GROUP
    };

#define GL_ATTACK_KIND_LONG FieldKind::Long, "J", 0U, 0U
#define GL_ATTACK_KIND_INT FieldKind::Int, "I", 0U, 0U
#define GL_ATTACK_KIND_BOOLEAN FieldKind::Boolean, "Z", 0U, 0U
#define GL_ATTACK_KIND_BYTES FieldKind::Bytes, "[B", 0U, 0U
#define GL_ATTACK_KIND_BYTES16 FieldKind::Bytes16, "[B", 16U, 16U
#define GL_ATTACK_KIND_BYTES32 FieldKind::Bytes32, "[B", 32U, 32U
#define GL_ATTACK_KIND_SECRET32 FieldKind::Secret32, "[B", 32U, 32U

    inline constexpr Field kFields[] = {
#define GL_ATTACK_GROUP(group_name)
#define GL_ATTACK_FIELD(group_name, field_name, field_kind) \
        {#group_name, #field_name, GL_ATTACK_KIND_##field_kind},
#define GL_ATTACK_BYTES_FIELD(group_name, field_name, max_bytes) \
        {#group_name, #field_name, FieldKind::Bytes, "[B", 0U, max_bytes},
#define GL_ATTACK_END_GROUP(group_name)
#include "contract/attack_contract_fields.hpp"
#undef GL_ATTACK_END_GROUP
#undef GL_ATTACK_BYTES_FIELD
#undef GL_ATTACK_FIELD
#undef GL_ATTACK_GROUP
    };

#undef GL_ATTACK_KIND_BYTES32
#undef GL_ATTACK_KIND_SECRET32
#undef GL_ATTACK_KIND_BYTES16
#undef GL_ATTACK_KIND_BYTES
#undef GL_ATTACK_KIND_BOOLEAN
#undef GL_ATTACK_KIND_INT
#undef GL_ATTACK_KIND_LONG

    inline constexpr std::size_t kGroupCount = sizeof(kGroups) / sizeof(kGroups[0]);
    inline constexpr std::size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

    /** @return group 是否只声明一次，且每组至少有一个字段。 */
    [[nodiscard]] constexpr bool groups_valid() noexcept {
        for (std::size_t left = 0U; left < kGroupCount; ++left) {
            std::size_t members = 0U;
            for (std::size_t field = 0U; field < kFieldCount; ++field) {
                if (std::string_view(kFields[field].group) == kGroups[left].name) {
                    ++members;
                }
            }
            if (members == 0U) {
                return false;
            }
            for (std::size_t right = left + 1U; right < kGroupCount; ++right) {
                if (std::string_view(kGroups[left].name) == kGroups[right].name) {
                    return false;
                }
            }
        }
        return true;
    }

    /** @return 每个字段是否属于已声明组，且组内字段名唯一。 */
    [[nodiscard]] constexpr bool fields_valid() noexcept {
        for (std::size_t left = 0U; left < kFieldCount; ++left) {
            bool declared = false;
            for (std::size_t group = 0U; group < kGroupCount; ++group) {
                declared = declared || std::string_view(kFields[left].group) == kGroups[group].name;
            }
            if (!declared || std::string_view(kFields[left].name).empty() ||
                (kFields[left].kind == FieldKind::Bytes &&
                 kFields[left].allocation_max_bytes == 0U) ||
                kFields[left].allocation_max_bytes < kFields[left].fixed_bytes) {
                return false;
            }
            for (std::size_t right = left + 1U; right < kFieldCount; ++right) {
                if (std::string_view(kFields[left].group) == kFields[right].group &&
                    std::string_view(kFields[left].name) == kFields[right].name) {
                    return false;
                }
            }
        }
        return true;
    }

    /**
     * @param group 参数对象名。
     * @return 该对象的字段数；未知对象返回 0。
     */
    [[nodiscard]] constexpr std::size_t field_count(const std::string_view group) noexcept {
        std::size_t result = 0U;
        for (const Field &field : kFields) {
            if (group == field.group) {
                ++result;
            }
        }
        return result;
    }

    /**
     * @param group 参数对象名。
     * @param name 字段名。
     * @return 组内序号；未知字段返回该组 field_count。
     */
    [[nodiscard]] constexpr std::size_t field_index(const std::string_view group,
                                                    const std::string_view name) noexcept {
        std::size_t ordinal = 0U;
        for (const Field &field : kFields) {
            if (group != field.group) {
                continue;
            }
            if (name == field.name) {
                return ordinal;
            }
            ++ordinal;
        }
        return ordinal;
    }

    /**
     * @param group 参数对象名。
     * @param ordinal 组内序号。
     * @return 字段描述；越界返回 nullptr。
     */
    [[nodiscard]] constexpr const Field *field_at(const std::string_view group,
                                                  const std::size_t ordinal) noexcept {
        std::size_t cursor = 0U;
        for (const Field &field : kFields) {
            if (group != field.group) {
                continue;
            }
            if (cursor == ordinal) {
                return &field;
            }
            ++cursor;
        }
        return nullptr;
    }

    static_assert(groups_valid());
    static_assert(fields_valid());
}  // namespace ghostlock::jni::contract
