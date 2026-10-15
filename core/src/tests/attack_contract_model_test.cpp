/** @file tests/attack_contract_model_test.cpp
 *  D1 具名读取器的逐字段分型哨兵与顺序负例。
 */

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include "jni/attack_contract_model.hpp"
#include "jni/attack_contract_values.hpp"

namespace {
    /** 与 JNI 源同接口的宿主字段源；顺序/类型不一致立即具名失败。 */
    class SentinelSource final {
    public:
        /**
         * @param group 读取组。
         * @param swap_at 相邻交换起点；等于字段数表示不交换。
         */
        SentinelSource(const std::string_view group, const std::size_t swap_at) {
            const std::size_t count = ghostlock::jni::contract::field_count(group);
            order_.reserve(count);
            for (std::size_t ordinal = 0U; ordinal < count; ++ordinal) {
                order_.push_back(ghostlock::jni::contract::field_at(group, ordinal));
            }
            if (swap_at + 1U < order_.size()) {
                std::swap(order_[swap_at], order_[swap_at + 1U]);
            }
        }

        /** @return long 字段按顺序写入唯一哨兵。 */
        [[nodiscard]] bool readLONG(const char *name, std::size_t fixed, std::size_t maximum,
                                    std::int64_t *out,
                                    ghostlock::jni::contract::ReadFailure *failure) {
            if (!accept(name, ghostlock::jni::contract::FieldKind::Long, fixed, maximum, failure)) {
                return false;
            }
            *out = long_sentinel(cursor_ - 1U);
            return true;
        }

        /** @return int 字段按顺序写入唯一哨兵。 */
        [[nodiscard]] bool readINT(const char *name, std::size_t fixed, std::size_t maximum,
                                  std::int32_t *out,
                                  ghostlock::jni::contract::ReadFailure *failure) {
            if (!accept(name, ghostlock::jni::contract::FieldKind::Int, fixed, maximum, failure)) {
                return false;
            }
            *out = int_sentinel(cursor_ - 1U);
            return true;
        }

        /** @return boolean 字段交替写入两个极端值。 */
        [[nodiscard]] bool readBOOLEAN(const char *name, std::size_t fixed, std::size_t maximum,
                                      bool *out,
                                      ghostlock::jni::contract::ReadFailure *failure) {
            if (!accept(name, ghostlock::jni::contract::FieldKind::Boolean, fixed, maximum,
                        failure)) {
                return false;
            }
            *out = boolean_sentinel(cursor_ - 1U);
            return true;
        }

        /** @return 变长 byte[] 写入字段专属内容。 */
        [[nodiscard]] bool readBYTES(const char *name, std::size_t fixed, std::size_t maximum,
                                    std::vector<std::uint8_t> *out,
                                    ghostlock::jni::contract::ReadFailure *failure) {
            return read_bytes(name, ghostlock::jni::contract::FieldKind::Bytes, fixed, maximum,
                              out, failure);
        }

        /** @return 16-byte byte[] 写入字段专属内容。 */
        [[nodiscard]] bool readBYTES16(const char *name, std::size_t fixed, std::size_t maximum,
                                      std::vector<std::uint8_t> *out,
                                      ghostlock::jni::contract::ReadFailure *failure) {
            return read_bytes(name, ghostlock::jni::contract::FieldKind::Bytes16, fixed, maximum,
                              out, failure);
        }

        /** @return 32-byte byte[] 写入字段专属内容。 */
        [[nodiscard]] bool readBYTES32(const char *name, std::size_t fixed, std::size_t maximum,
                                      std::vector<std::uint8_t> *out,
                                      ghostlock::jni::contract::ReadFailure *failure) {
            return read_bytes(name, ghostlock::jni::contract::FieldKind::Bytes32, fixed, maximum,
                              out, failure);
        }

        /** @return 32-byte secret 写入 zeroizing buffer。 */
        [[nodiscard]] bool readSECRET32(const char *name, std::size_t fixed, std::size_t maximum,
                                       ghostlock::jni::contract::ZeroizingBytes32 *out,
                                       ghostlock::jni::contract::ReadFailure *failure) {
            if (!accept(name, ghostlock::jni::contract::FieldKind::Secret32, fixed, maximum,
                        failure)) {
                return false;
            }
            const std::vector<std::uint8_t> sentinel = bytes_sentinel(cursor_ - 1U, fixed);
            std::copy(sentinel.begin(), sentinel.end(), out->data());
            return true;
        }

        /** @return 已消费字段数。 */
        [[nodiscard]] std::size_t consumed() const noexcept {
            return cursor_;
        }

        /** @return long 的字段专属哨兵。 */
        [[nodiscard]] static constexpr std::int64_t long_sentinel(const std::size_t ordinal) noexcept {
            return static_cast<std::int64_t>(0x4A4E490000000000ULL + ordinal + 1U);
        }

        /** @return int 的字段专属哨兵。 */
        [[nodiscard]] static constexpr std::int32_t int_sentinel(const std::size_t ordinal) noexcept {
            return static_cast<std::int32_t>(0x4A000000U + ordinal + 1U);
        }

        /** @return boolean 的分型极端哨兵。 */
        [[nodiscard]] static constexpr bool boolean_sentinel(const std::size_t ordinal) noexcept {
            return ordinal % 2U != 0U;
        }

        /** @return byte[] 的字段专属内容。 */
        [[nodiscard]] static std::vector<std::uint8_t> bytes_sentinel(
            const std::size_t ordinal, const std::size_t fixed) {
            const std::size_t size = fixed == 0U ? ordinal % 7U + 1U : fixed;
            return std::vector<std::uint8_t>(size, static_cast<std::uint8_t>(ordinal + 1U));
        }

    private:
        /** @return 请求字段与夹具当前字段的名字、类型均一致。 */
        [[nodiscard]] bool accept(const char *name, const ghostlock::jni::contract::FieldKind kind,
                                  const std::size_t fixed, const std::size_t maximum,
                                  ghostlock::jni::contract::ReadFailure *failure) {
            if (cursor_ >= order_.size() || std::string_view(order_[cursor_]->name) != name ||
                order_[cursor_]->kind != kind || order_[cursor_]->fixed_bytes != fixed ||
                order_[cursor_]->allocation_max_bytes != maximum) {
                failure->error = ghostlock::jni::contract::ReadError::FieldOrderMismatch;
                failure->field = name;
                return false;
            }
            ++cursor_;
            return true;
        }

        /** @return byte[] 字段按类型与定长要求写入专属内容。 */
        [[nodiscard]] bool read_bytes(const char *name,
                                      const ghostlock::jni::contract::FieldKind kind,
                                      const std::size_t fixed, const std::size_t maximum,
                                      std::vector<std::uint8_t> *out,
                                      ghostlock::jni::contract::ReadFailure *failure) {
            if (!accept(name, kind, fixed, maximum, failure)) {
                return false;
            }
            *out = bytes_sentinel(cursor_ - 1U, fixed);
            return true;
        }

        std::vector<const ghostlock::jni::contract::Field *> order_;
        std::size_t cursor_{};
    };

    /** 验证 long 具名成员收到本字段哨兵。 */
    void assert_LONG(const std::int64_t value, const std::string_view group, const char *field) {
        assert(value == SentinelSource::long_sentinel(
                            ghostlock::jni::contract::field_index(group, field)));
    }

    /** 验证 int 具名成员收到本字段哨兵。 */
    void assert_INT(const std::int32_t value, const std::string_view group, const char *field) {
        assert(value == SentinelSource::int_sentinel(
                            ghostlock::jni::contract::field_index(group, field)));
    }

    /** 验证 boolean 具名成员收到分型极端哨兵。 */
    void assert_BOOLEAN(const bool value, const std::string_view group, const char *field) {
        assert(value == SentinelSource::boolean_sentinel(
                            ghostlock::jni::contract::field_index(group, field)));
    }

    /** 验证变长 byte[] 具名成员收到本字段内容。 */
    void assert_BYTES(const std::vector<std::uint8_t> &value, const std::string_view group,
                      const char *field) {
        const std::size_t ordinal = ghostlock::jni::contract::field_index(group, field);
        assert(value == SentinelSource::bytes_sentinel(ordinal, 0U));
    }

    /** 验证 16-byte byte[] 的长度与字段内容。 */
    void assert_BYTES16(const std::vector<std::uint8_t> &value, const std::string_view group,
                        const char *field) {
        const std::size_t ordinal = ghostlock::jni::contract::field_index(group, field);
        assert(value == SentinelSource::bytes_sentinel(ordinal, 16U));
    }

    /** 验证 32-byte byte[] 的长度与字段内容。 */
    void assert_BYTES32(const std::vector<std::uint8_t> &value, const std::string_view group,
                        const char *field) {
        const std::size_t ordinal = ghostlock::jni::contract::field_index(group, field);
        assert(value == SentinelSource::bytes_sentinel(ordinal, 32U));
    }

    /** 验证 32-byte secret 具名成员收到哨兵且使用 zeroizing 类型。 */
    void assert_SECRET32(const ghostlock::jni::contract::ZeroizingBytes32 &value,
                         const std::string_view group, const char *field) {
        const std::size_t ordinal = ghostlock::jni::contract::field_index(group, field);
        assert(value.equals(SentinelSource::bytes_sentinel(ordinal, 32U)));
    }
}  // namespace

/** @return 所有具名成员哨兵与交换负例均通过时为 0。 */
int main() {
    assert(ghostlock::jni::contract::groups_valid());
    assert(ghostlock::jni::contract::fields_valid());
    for (const ghostlock::jni::contract::Field &field : ghostlock::jni::contract::kFields) {
        if (field.kind == ghostlock::jni::contract::FieldKind::Bytes) {
            assert(field.allocation_max_bytes > 0U);
            assert(ghostlock::jni::contract::validate_byte_length(
                       -1, 0U, field.allocation_max_bytes) ==
                   ghostlock::jni::contract::ReadError::TooLarge);
            assert(ghostlock::jni::contract::validate_byte_length(
                       0, 0U, field.allocation_max_bytes) ==
                   ghostlock::jni::contract::ReadError::Ok);
            assert(ghostlock::jni::contract::validate_byte_length(
                       static_cast<std::int64_t>(field.allocation_max_bytes), 0U,
                       field.allocation_max_bytes) ==
                   ghostlock::jni::contract::ReadError::Ok);
            assert(ghostlock::jni::contract::validate_byte_length(
                       static_cast<std::int64_t>(field.allocation_max_bytes + 1U), 0U,
                       field.allocation_max_bytes) ==
                   ghostlock::jni::contract::ReadError::TooLarge);
        }
        if (field.fixed_bytes != 0U) {
            assert(ghostlock::jni::contract::validate_byte_length(
                       static_cast<std::int64_t>(field.fixed_bytes - 1U), field.fixed_bytes,
                       field.allocation_max_bytes) ==
                   ghostlock::jni::contract::ReadError::InvalidByteLength);
            assert(ghostlock::jni::contract::validate_byte_length(
                       static_cast<std::int64_t>(field.fixed_bytes), field.fixed_bytes,
                       field.allocation_max_bytes) ==
                   ghostlock::jni::contract::ReadError::Ok);
            assert(ghostlock::jni::contract::validate_byte_length(
                       static_cast<std::int64_t>(field.fixed_bytes + 1U), field.fixed_bytes,
                       field.allocation_max_bytes) ==
                   ghostlock::jni::contract::ReadError::TooLarge);
        }
    }

    {
        ghostlock::jni::contract::ReadBlock43284Values unchanged{};
        unchanged.ticket = 0x1122334455667788LL;
        unchanged.offset = 0x7766554433221100LL;
        SentinelSource swapped("ReadBlock43284", 0U);
        ghostlock::jni::contract::ReadFailure failure{};
        assert(!ghostlock::jni::contract::readReadBlock43284From(swapped, &unchanged, &failure));
        assert(unchanged.ticket == 0x1122334455667788LL);
        assert(unchanged.offset == 0x7766554433221100LL);
    }

    {
        ghostlock::jni::contract::ZeroizingBytes32 source{};
        std::fill_n(source.data(), source.size(), static_cast<std::uint8_t>(0xA5U));
        ghostlock::jni::contract::ZeroizingBytes32 destination(std::move(source));
        for (std::size_t index = 0U; index < source.size(); ++index) {
            assert(source.data()[index] == 0U);
            assert(destination.data()[index] == 0xA5U);
        }
        std::fill_n(source.data(), source.size(), static_cast<std::uint8_t>(0x5AU));
        destination = std::move(source);
        for (std::size_t index = 0U; index < source.size(); ++index) {
            assert(source.data()[index] == 0U);
            assert(destination.data()[index] == 0x5AU);
        }
        destination.clear();
        for (std::size_t index = 0U; index < destination.size(); ++index) {
            assert(destination.data()[index] == 0U);
        }
    }

#define GL_ATTACK_GROUP(group_name) \
    { \
        const std::size_t count = ghostlock::jni::contract::field_count(#group_name); \
        SentinelSource source(#group_name, count); \
        ghostlock::jni::contract::group_name##Values values{}; \
        ghostlock::jni::contract::ReadFailure failure{}; \
        assert(ghostlock::jni::contract::read##group_name##From(source, &values, &failure)); \
        assert(source.consumed() == count);
#define GL_ATTACK_FIELD(group_name, field_name, field_kind) \
        assert_##field_kind(values.field_name, #group_name, #field_name);
#define GL_ATTACK_BYTES_FIELD(group_name, field_name, max_bytes) \
        assert_BYTES(values.field_name, #group_name, #field_name);
#define GL_ATTACK_END_GROUP(group_name) \
        for (std::size_t swap_at = 0U; swap_at + 1U < count; ++swap_at) { \
            SentinelSource swapped(#group_name, swap_at); \
            ghostlock::jni::contract::group_name##Values rejected{}; \
            ghostlock::jni::contract::ReadFailure rejected_failure{}; \
            assert(!ghostlock::jni::contract::read##group_name##From( \
                swapped, &rejected, &rejected_failure)); \
            assert(rejected_failure.error == \
                   ghostlock::jni::contract::ReadError::FieldOrderMismatch); \
        } \
    }
#include "jni/attack_contract_fields.def"
#undef GL_ATTACK_END_GROUP
#undef GL_ATTACK_BYTES_FIELD
#undef GL_ATTACK_FIELD
#undef GL_ATTACK_GROUP
    return 0;
}
