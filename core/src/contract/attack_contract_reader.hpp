#pragma once
/** @file contract/attack_contract_reader.hpp
 *  D1 具名值读取器的 JNI 字段源。
 *
 *  @note byte[] 在本函数内复制进 native vector 并立即释放 local reference；任何 JNI 异常都清除后
 *  返回 JniException。调用方只得到拥有自身存储的具名 Values，不保存 JVM 引用或 ordinal 数组。
 */

#include <cstddef>
#include <cstdint>
#include <array>
#include <new>
#include <vector>

#include <jni.h>

#include "contract/attack_contract_values.hpp"

namespace ghostlock::jni::contract {
    /** 把 Kotlin 对象投影为 read<Group>From 可消费的字段源。 */
    class JniFieldSource final {
    public:
        /**
         * @param env 当前 JNI 环境。
         * @param source Kotlin 生成 DTO；不得为 null。
         * @note ready() 为 false 时不得再读字段。
         */
        JniFieldSource(JNIEnv *env, jobject source) noexcept
            : env_(env), source_(source),
              object_class_(source == nullptr ? nullptr : env->GetObjectClass(source)) {
            if (env_->ExceptionCheck() == JNI_TRUE) {
                env_->ExceptionClear();
                exception_on_create_ = true;
            }
        }

        JniFieldSource(const JniFieldSource &) = delete;
        JniFieldSource &operator=(const JniFieldSource &) = delete;

        /** 删除本字段源唯一持有的 class local reference。 */
        ~JniFieldSource() {
            if (object_class_ != nullptr) {
                env_->DeleteLocalRef(object_class_);
            }
        }

        /** @return DTO 与 class 均有效且构造期无 JNI 异常。 */
        [[nodiscard]] bool ready(ReadFailure *failure) const noexcept {
            if (source_ == nullptr) {
                failure->error = ReadError::NullObject;
                failure->field = nullptr;
                return false;
            }
            if (exception_on_create_ || object_class_ == nullptr) {
                failure->error = ReadError::JniException;
                failure->field = nullptr;
                return false;
            }
            return true;
        }

        /** @return 名为 name 的 long 字段读取成功。 */
        [[nodiscard]] bool readLONG(const char *name, std::size_t, std::size_t, std::int64_t *out,
                                    ReadFailure *failure) noexcept {
            jfieldID id = field(name, "J", failure);
            if (id == nullptr) {
                return false;
            }
            const jlong value = env_->GetLongField(source_, id);
            if (!finish_jni(name, failure)) {
                return false;
            }
            *out = static_cast<std::int64_t>(value);
            return true;
        }

        /** @return 名为 name 的 int 字段读取成功。 */
        [[nodiscard]] bool readINT(const char *name, std::size_t, std::size_t, std::int32_t *out,
                                  ReadFailure *failure) noexcept {
            jfieldID id = field(name, "I", failure);
            if (id == nullptr) {
                return false;
            }
            const jint value = env_->GetIntField(source_, id);
            if (!finish_jni(name, failure)) {
                return false;
            }
            *out = static_cast<std::int32_t>(value);
            return true;
        }

        /** @return 名为 name 的 boolean 字段读取成功。 */
        [[nodiscard]] bool readBOOLEAN(const char *name, std::size_t, std::size_t, bool *out,
                                      ReadFailure *failure) noexcept {
            jfieldID id = field(name, "Z", failure);
            if (id == nullptr) {
                return false;
            }
            const jboolean value = env_->GetBooleanField(source_, id);
            if (!finish_jni(name, failure)) {
                return false;
            }
            *out = value == JNI_TRUE;
            return true;
        }

        /** @return 名为 name 的 byte[] 已复制且长度满足 fixed_bytes。 */
        [[nodiscard]] bool readBYTES(const char *name, const std::size_t fixed_bytes,
                                    const std::size_t max_bytes,
                                    std::vector<std::uint8_t> *out,
                                    ReadFailure *failure) noexcept {
            return read_bytes(name, fixed_bytes, max_bytes, out, failure);
        }

        /** @return 名为 name 的 16-byte byte[] 已复制。 */
        [[nodiscard]] bool readBYTES16(const char *name, const std::size_t fixed_bytes,
                                      const std::size_t max_bytes,
                                      std::vector<std::uint8_t> *out,
                                      ReadFailure *failure) noexcept {
            return read_bytes(name, fixed_bytes, max_bytes, out, failure);
        }

        /** @return 名为 name 的 32-byte byte[] 已复制。 */
        [[nodiscard]] bool readBYTES32(const char *name, const std::size_t fixed_bytes,
                                      const std::size_t max_bytes,
                                      std::vector<std::uint8_t> *out,
                                      ReadFailure *failure) noexcept {
            return read_bytes(name, fixed_bytes, max_bytes, out, failure);
        }

        /** @return 名为 name 的 32-byte 秘密已直接复制进 zeroizing buffer。 */
        [[nodiscard]] bool readSECRET32(const char *name, const std::size_t fixed_bytes,
                                       const std::size_t max_bytes, ZeroizingBytes32 *out,
                                       ReadFailure *failure) noexcept {
            if (fixed_bytes != ZeroizingBytes32::size() || max_bytes != fixed_bytes) {
                failure->error = ReadError::InvalidByteLength;
                failure->field = name;
                return false;
            }
            jbyteArray bytes = byte_array(name, fixed_bytes, max_bytes, failure);
            if (bytes == nullptr) {
                return false;
            }
            env_->GetByteArrayRegion(bytes, 0, static_cast<jsize>(fixed_bytes),
                                     reinterpret_cast<jbyte *>(out->data()));
            const bool copied = finish_jni(name, failure);
            env_->DeleteLocalRef(bytes);
            if (!copied) {
                out->clear();
            }
            return copied;
        }

        /** 清零 Kotlin DTO 中名为 name 的 32-byte secret；清理失败也不遗留 Java 异常。 */
        void clearSECRET32(const char *name) noexcept {
            jfieldID id = env_->GetFieldID(object_class_, name, "[B");
            if (id == nullptr) {
                if (env_->ExceptionCheck() == JNI_TRUE) {
                    env_->ExceptionClear();
                }
                return;
            }
            jobject object = env_->GetObjectField(source_, id);
            if (env_->ExceptionCheck() == JNI_TRUE) {
                env_->ExceptionClear();
                return;
            }
            if (object == nullptr) {
                return;
            }
            jbyteArray bytes = static_cast<jbyteArray>(object);
            if (env_->GetArrayLength(bytes) == static_cast<jsize>(ZeroizingBytes32::size())) {
                std::array<jbyte, 32U> zeros{};
                env_->SetByteArrayRegion(bytes, 0, static_cast<jsize>(zeros.size()), zeros.data());
                if (env_->ExceptionCheck() == JNI_TRUE) {
                    env_->ExceptionClear();
                }
            }
            env_->DeleteLocalRef(bytes);
        }

    private:
        /** @return 字段 ID；缺字段或 JNI 异常统一返回 null/JniException。 */
        [[nodiscard]] jfieldID field(const char *name, const char *signature,
                                     ReadFailure *failure) noexcept {
            jfieldID id = env_->GetFieldID(object_class_, name, signature);
            if (id == nullptr) {
                const bool had_exception = env_->ExceptionCheck() == JNI_TRUE;
                if (had_exception) {
                    env_->ExceptionClear();
                }
                failure->error = ReadError::JniException;
                failure->field = name;
            }
            return id;
        }

        /** @return 当前 JNI 操作没有异常；异常会被清除并转成稳定失败。 */
        [[nodiscard]] bool finish_jni(const char *name, ReadFailure *failure) noexcept {
            if (env_->ExceptionCheck() != JNI_TRUE) {
                return true;
            }
            env_->ExceptionClear();
            failure->error = ReadError::JniException;
            failure->field = name;
            return false;
        }

        /** @return byte[] 成功复制进 out；local reference 在所有路径释放。 */
        [[nodiscard]] jbyteArray byte_array(const char *name, const std::size_t fixed_bytes,
                                            const std::size_t max_bytes,
                                            ReadFailure *failure) noexcept {
            jfieldID id = field(name, "[B", failure);
            if (id == nullptr) {
                return nullptr;
            }
            jobject object = env_->GetObjectField(source_, id);
            if (!finish_jni(name, failure)) {
                return nullptr;
            }
            if (object == nullptr) {
                failure->error = ReadError::NullBytes;
                failure->field = name;
                return nullptr;
            }
            jbyteArray bytes = static_cast<jbyteArray>(object);
            const jsize size = env_->GetArrayLength(bytes);
            if (!finish_jni(name, failure)) {
                env_->DeleteLocalRef(bytes);
                return nullptr;
            }
            const ReadError length_error =
                validate_byte_length(static_cast<std::int64_t>(size), fixed_bytes, max_bytes);
            if (length_error != ReadError::Ok) {
                env_->DeleteLocalRef(bytes);
                failure->error = length_error;
                failure->field = name;
                return nullptr;
            }
            return bytes;
        }

        /** @return byte[] 成功复制进 out；local reference 在所有路径释放。 */
        [[nodiscard]] bool read_bytes(const char *name, const std::size_t fixed_bytes,
                                      const std::size_t max_bytes,
                                      std::vector<std::uint8_t> *out,
                                      ReadFailure *failure) noexcept {
            jbyteArray bytes = byte_array(name, fixed_bytes, max_bytes, failure);
            if (bytes == nullptr) {
                return false;
            }
            const jsize size = env_->GetArrayLength(bytes);
            if (!finish_jni(name, failure)) {
                env_->DeleteLocalRef(bytes);
                return false;
            }
            try {
                out->resize(static_cast<std::size_t>(size));
            } catch (const std::bad_alloc &) {
                env_->DeleteLocalRef(bytes);
                failure->error = ReadError::AllocationFailed;
                failure->field = name;
                return false;
            }
            if (size > 0) {
                env_->GetByteArrayRegion(bytes, 0, size, reinterpret_cast<jbyte *>(out->data()));
            }
            const bool copied = finish_jni(name, failure);
            env_->DeleteLocalRef(bytes);
            if (!copied) {
                out->clear();
            }
            return copied;
        }

        JNIEnv *env_;
        jobject source_;
        jclass object_class_;
        bool exception_on_create_{};
    };

#define GL_ATTACK_CLEAR_LONG(fields, name)
#define GL_ATTACK_CLEAR_INT(fields, name)
#define GL_ATTACK_CLEAR_BOOLEAN(fields, name)
#define GL_ATTACK_CLEAR_BYTES(fields, name)
#define GL_ATTACK_CLEAR_BYTES16(fields, name)
#define GL_ATTACK_CLEAR_BYTES32(fields, name)
#define GL_ATTACK_CLEAR_SECRET32(fields, name) fields.clearSECRET32(name)
#define GL_ATTACK_GROUP(group_name) \
    /** 清零 group_name DTO 的全部 secret 字段。 */ \
    inline void clear##group_name##Secrets(JniFieldSource &fields) noexcept { \
        static_cast<void>(fields);
#define GL_ATTACK_FIELD(group_name, field_name, field_kind) \
        GL_ATTACK_CLEAR_##field_kind(fields, #field_name);
#define GL_ATTACK_BYTES_FIELD(group_name, field_name, max_bytes)
#define GL_ATTACK_END_GROUP(group_name) }
#include "contract/attack_contract_fields.hpp"
#undef GL_ATTACK_END_GROUP
#undef GL_ATTACK_BYTES_FIELD
#undef GL_ATTACK_FIELD
#undef GL_ATTACK_GROUP
#undef GL_ATTACK_CLEAR_SECRET32
#undef GL_ATTACK_CLEAR_BYTES32
#undef GL_ATTACK_CLEAR_BYTES16
#undef GL_ATTACK_CLEAR_BYTES
#undef GL_ATTACK_CLEAR_BOOLEAN
#undef GL_ATTACK_CLEAR_INT
#undef GL_ATTACK_CLEAR_LONG

#define GL_ATTACK_GROUP(group_name) \
    /** @return Kotlin DTO 已完整复制到具名 group_name Values。 */ \
    [[nodiscard]] inline bool read##group_name(JNIEnv *env, jobject source, \
                                               group_name##Values *out, \
                                               ReadFailure *failure) noexcept { \
        JniFieldSource fields(env, source); \
        const bool result = fields.ready(failure) && read##group_name##From(fields, out, failure); \
        if (fields.ready(failure)) { \
            clear##group_name##Secrets(fields); \
        } \
        return result; \
    }
#define GL_ATTACK_FIELD(group_name, field_name, field_kind)
#define GL_ATTACK_BYTES_FIELD(group_name, field_name, max_bytes)
#define GL_ATTACK_END_GROUP(group_name)
#include "contract/attack_contract_fields.hpp"
#undef GL_ATTACK_END_GROUP
#undef GL_ATTACK_BYTES_FIELD
#undef GL_ATTACK_FIELD
#undef GL_ATTACK_GROUP
}  // namespace ghostlock::jni::contract
