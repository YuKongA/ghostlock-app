#pragma once
/** @file jni/attack_contract_values.hpp
 *  D1 单表生成的具名 native 值对象与可替换字段源读取器。
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace ghostlock::jni::contract {
    /** 读取失败的稳定分类。 */
    enum class ReadError : std::uint8_t {
        Ok,
        NullObject,
        JniException,
        NullBytes,
        InvalidByteLength,
        FieldOrderMismatch,
        TooLarge,
        AllocationFailed,
    };

    /** 失败字段与分类；字段名来自唯一字段表，生命周期为进程期。 */
    struct ReadFailure final {
        ReadError error{ReadError::Ok};
        const char *field{};
    };

    /**
     * @param size JNI byte[] 长度；负值非法。
     * @param fixed_bytes 0 表示变长，否则必须精确相等。
     * @param max_bytes 分配前硬上限。
     * @return Ok、TooLarge 或 InvalidByteLength。
     */
    [[nodiscard]] constexpr ReadError validate_byte_length(const std::int64_t size,
                                                           const std::size_t fixed_bytes,
                                                           const std::size_t max_bytes) noexcept {
        if (size < 0 || static_cast<std::uint64_t>(size) > max_bytes) {
            return ReadError::TooLarge;
        }
        if (fixed_bytes != 0U && static_cast<std::size_t>(size) != fixed_bytes) {
            return ReadError::InvalidByteLength;
        }
        return ReadError::Ok;
    }

    /** 固定 32-byte 秘密；销毁、覆盖及 move 后源对象都会显式擦除。 */
    class ZeroizingBytes32 final {
    public:
        ZeroizingBytes32() = default;
        ZeroizingBytes32(const ZeroizingBytes32 &) = delete;
        ZeroizingBytes32 &operator=(const ZeroizingBytes32 &) = delete;

        /** move 后立即擦除源对象。 */
        ZeroizingBytes32(ZeroizingBytes32 &&other) noexcept : bytes_(other.bytes_) {
            other.clear();
        }

        /** 覆盖前擦除当前值，move 后擦除源对象。 */
        ZeroizingBytes32 &operator=(ZeroizingBytes32 &&other) noexcept {
            if (this != &other) {
                clear();
                bytes_ = other.bytes_;
                other.clear();
            }
            return *this;
        }

        /** 擦除秘密。 */
        ~ZeroizingBytes32() {
            clear();
        }

        /** @return 可写 32-byte 存储。 */
        [[nodiscard]] std::uint8_t *data() noexcept {
            return bytes_.data();
        }

        /** @return 只读 32-byte 存储。 */
        [[nodiscard]] const std::uint8_t *data() const noexcept {
            return bytes_.data();
        }

        /** @return 固定大小 32。 */
        [[nodiscard]] static constexpr std::size_t size() noexcept {
            return 32U;
        }

        /** @return 与普通字节向量内容相同。 */
        [[nodiscard]] bool equals(const std::vector<std::uint8_t> &other) const noexcept {
            if (other.size() != bytes_.size()) {
                return false;
            }
            for (std::size_t index = 0U; index < bytes_.size(); ++index) {
                if (bytes_[index] != other[index]) {
                    return false;
                }
            }
            return true;
        }

        /** 用不可被普通 dead-store elimination 删除的写入擦除。 */
        void clear() noexcept {
            volatile std::uint8_t *cursor = bytes_.data();
            for (std::size_t index = 0U; index < bytes_.size(); ++index) {
                cursor[index] = 0U;
            }
        }

    private:
        std::array<std::uint8_t, 32U> bytes_{};
    };

#define GL_ATTACK_CPP_LONG std::int64_t
#define GL_ATTACK_CPP_INT std::int32_t
#define GL_ATTACK_CPP_BOOLEAN bool
#define GL_ATTACK_CPP_BYTES std::vector<std::uint8_t>
#define GL_ATTACK_CPP_BYTES16 std::vector<std::uint8_t>
#define GL_ATTACK_CPP_BYTES32 std::vector<std::uint8_t>
#define GL_ATTACK_CPP_SECRET32 ZeroizingBytes32
#define GL_ATTACK_GROUP(group_name) struct group_name##Values final {
#define GL_ATTACK_FIELD(group_name, field_name, field_kind) \
        GL_ATTACK_CPP_##field_kind field_name{};
#define GL_ATTACK_BYTES_FIELD(group_name, field_name, max_bytes) \
        std::vector<std::uint8_t> field_name{};
#define GL_ATTACK_END_GROUP(group_name) };
#include "jni/attack_contract_fields.def"
#undef GL_ATTACK_END_GROUP
#undef GL_ATTACK_BYTES_FIELD
#undef GL_ATTACK_FIELD
#undef GL_ATTACK_GROUP
#undef GL_ATTACK_CPP_BYTES32
#undef GL_ATTACK_CPP_SECRET32
#undef GL_ATTACK_CPP_BYTES16
#undef GL_ATTACK_CPP_BYTES
#undef GL_ATTACK_CPP_BOOLEAN
#undef GL_ATTACK_CPP_INT
#undef GL_ATTACK_CPP_LONG

#define GL_ATTACK_FIXED_LONG 0U
#define GL_ATTACK_FIXED_INT 0U
#define GL_ATTACK_FIXED_BOOLEAN 0U
#define GL_ATTACK_FIXED_BYTES 0U
#define GL_ATTACK_FIXED_BYTES16 16U
#define GL_ATTACK_FIXED_BYTES32 32U
#define GL_ATTACK_FIXED_SECRET32 32U
#define GL_ATTACK_MAX_LONG 0U
#define GL_ATTACK_MAX_INT 0U
#define GL_ATTACK_MAX_BOOLEAN 0U
#define GL_ATTACK_MAX_BYTES 0U
#define GL_ATTACK_MAX_BYTES16 16U
#define GL_ATTACK_MAX_BYTES32 32U
#define GL_ATTACK_MAX_SECRET32 32U
#define GL_ATTACK_GROUP(group_name) \
    /** @return source 按唯一字段表完整填入 group_name 时为 true。 */ \
    template <typename Source> \
    [[nodiscard]] bool read##group_name##From(Source &source, group_name##Values *out, \
                                               ReadFailure *failure) { \
        group_name##Values temporary{}; \
        failure->error = ReadError::Ok; \
        failure->field = nullptr;
#define GL_ATTACK_FIELD(group_name, field_name, field_kind) \
        if (!source.read##field_kind(#field_name, GL_ATTACK_FIXED_##field_kind, \
                                     GL_ATTACK_MAX_##field_kind, &temporary.field_name, failure)) { \
            return false; \
        }
#define GL_ATTACK_BYTES_FIELD(group_name, field_name, max_bytes) \
        if (!source.readBYTES(#field_name, 0U, max_bytes, &temporary.field_name, failure)) { \
            return false; \
        }
#define GL_ATTACK_END_GROUP(group_name) \
        *out = std::move(temporary); \
        return true; \
    }
#include "jni/attack_contract_fields.def"
#undef GL_ATTACK_END_GROUP
#undef GL_ATTACK_BYTES_FIELD
#undef GL_ATTACK_FIELD
#undef GL_ATTACK_GROUP
#undef GL_ATTACK_FIXED_BYTES32
#undef GL_ATTACK_FIXED_SECRET32
#undef GL_ATTACK_FIXED_BYTES16
#undef GL_ATTACK_FIXED_BYTES
#undef GL_ATTACK_FIXED_BOOLEAN
#undef GL_ATTACK_FIXED_INT
#undef GL_ATTACK_FIXED_LONG
#undef GL_ATTACK_MAX_SECRET32
#undef GL_ATTACK_MAX_BYTES32
#undef GL_ATTACK_MAX_BYTES16
#undef GL_ATTACK_MAX_BYTES
#undef GL_ATTACK_MAX_BOOLEAN
#undef GL_ATTACK_MAX_INT
#undef GL_ATTACK_MAX_LONG
}  // namespace ghostlock::jni::contract
