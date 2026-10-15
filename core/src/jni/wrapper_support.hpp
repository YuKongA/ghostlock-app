#pragma once
/** @file jni/wrapper_support.hpp
 *  每个 backend 一个 JNI 库所需要的那点公共机械：句柄表、数组读写、名字表。
 *
 *  句柄与帧无关，所以这里的实现直接从旧的边界继承（索引 + 代）：拿错句柄是 stale-handle，
 *  而不是"另一台机器的张冠李戴"。数组边界一律校验：Kotlin 给的数组短了就是 bad-parameters，
 *  不做越界写 —— 这是跨端传参唯一必须守住的东西。
 */
#include <cstddef>
#include <cstdint>
#include <jni.h>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

#include "jni/backend_api.hpp"

namespace ghostlock::jni {
    namespace wrapper {
        /** 把 jlongArray 读进定长缓冲；短了就回答 false（调用方给 bad-parameters）。 */
        [[nodiscard]] inline bool read_longs(JNIEnv *env, jlongArray array, jlong *out,
                                             const std::size_t need) noexcept {
            if (array == nullptr || out == nullptr) {
                return false;
            }
            const jsize length = env->GetArrayLength(array);
            if (length < static_cast<jsize>(need)) {
                return false;
            }
            env->GetLongArrayRegion(array, 0, static_cast<jsize>(need), out);
            return env->ExceptionCheck() == JNI_FALSE;
        }

        /** 把若干 long 写进调用方给的数组；短了就回答 false（调用方给 bad-parameters）。 */
        [[nodiscard]] inline bool write_longs(JNIEnv *env, jlongArray array, const jlong *values,
                                              const std::size_t count) noexcept {
            if (array == nullptr) {
                return false;
            }
            if (env->GetArrayLength(array) < static_cast<jsize>(count)) {
                return false;
            }
            env->SetLongArrayRegion(array, 0, static_cast<jsize>(count), values);
            return env->ExceptionCheck() == JNI_FALSE;
        }

        /** 造一个 String[]（字段布局自描述）。 */
        [[nodiscard]] inline jobjectArray make_string_array(JNIEnv *env,
                                                            const char *const *names,
                                                            const std::size_t count) noexcept {
            jclass string_class = env->FindClass("java/lang/String");
            if (string_class == nullptr) {
                return nullptr;
            }
            jobjectArray array = env->NewObjectArray(static_cast<jsize>(count), string_class, nullptr);
            if (array == nullptr) {
                return nullptr;
            }
            for (std::size_t index = 0U; index < count; ++index) {
                jstring item = env->NewStringUTF(names[index]);
                env->SetObjectArrayElement(array, static_cast<jsize>(index), item);
                env->DeleteLocalRef(item);
            }
            return array;
        }

        /**
         *  一个 backend 库的会话表：句柄 = 索引 + 代。
         *
         *  @tparam Backend 该 backend 的会话类型（Backend43499 / Backend43284）。
         */
        template <class Backend>
        class Registry final {
          public:
            /** 建一个会话。@return 句柄（永远 > 0）。 */
            [[nodiscard]] std::uint64_t create() noexcept {
                std::lock_guard<std::mutex> guard(mutex_);
                for (std::size_t index = 0U; index < slots_.size(); ++index) {
                    if (!slots_[index].live) {
                        slots_[index].live = true;
                        slots_[index].generation += 1U;
                        slots_[index].backend = Backend{};
                        return make_handle(index, slots_[index].generation);
                    }
                }
                slots_.push_back(Slot{});
                slots_.back().live = true;
                slots_.back().generation = 1U;
                slots_.back().backend = Backend{};
                return make_handle(slots_.size() - 1U, 1U);
            }

            /** 回收一个会话。@return 是否回收成功（未知句柄 = false）。 */
            [[nodiscard]] bool retire(const std::uint64_t handle) noexcept {
                std::lock_guard<std::mutex> guard(mutex_);
                Slot *const slot = find(handle);
                if (slot == nullptr) {
                    return false;
                }
                slot->live = false;
                slot->backend = Backend{};
                return true;
            }

            /**
             *  在一次调用里持有会话的表锁。
             *
             *  @param work 收到会话引用；返回调用码。
             *  @return 句柄不对时 StaleHandle；否则是 work 的答案。
             */
            template <class Work>
            [[nodiscard]] CallCode with(const std::uint64_t handle, Work &&work) noexcept {
                std::lock_guard<std::mutex> guard(mutex_);
                Slot *const slot = find(handle);
                if (slot == nullptr) {
                    return CallCode::StaleHandle;
                }
                return work(slot->backend);
            }

          private:
            struct Slot final {
                bool live = false;
                std::uint64_t generation = 0U;
                Backend backend{};
            };

            [[nodiscard]] static std::uint64_t make_handle(const std::size_t index,
                                                           const std::uint64_t generation) noexcept {
                /* 1 起编：0 留给"没有句柄"。 */
                return (generation << 32U) | (static_cast<std::uint64_t>(index) + 1U);
            }

            /** @return 该句柄指向的槽，或 null（不存在 / 已回收 / 代不对）。 */
            [[nodiscard]] Slot *find(const std::uint64_t handle) noexcept {
                const std::uint64_t index = (handle & 0xFFFFFFFFU) - 1U;
                const std::uint64_t generation = handle >> 32U;
                if (handle == 0U || index >= slots_.size()) {
                    return nullptr;
                }
                Slot &slot = slots_[index];
                if (!slot.live || slot.generation != generation) {
                    return nullptr;
                }
                return &slot;
            }

            std::mutex mutex_{};
            std::vector<Slot> slots_{};
        };
    }  // namespace wrapper
}  // namespace ghostlock::jni
