#pragma once
/** @file jni/wrapper_support.hpp
 *  每个 backend 一个 JNI 库所需要的那点公共机械：句柄表、数组读写、原子答案。
 *
 *  这一层解决四件事，每件都是审查点名的：
 *
 *  1. **并发单位是句柄，不是整张表**（每槽一把锁）：一个会话的调用不该让别的会话排队。
 *     加锁顺序固定为"表 → 槽"；回收路径先把槽标成 dead 再取槽锁，于是在飞的那一次调用会先跑完，
 *     之后才释放资源 —— 不会出现"调用还在跑、对象已经被清掉"。
 *  2. **销毁先关闭**：retire 内部先调 backend.shutdown()，再清状态。调用方忘了关，设备侧资源也
 *     必须在回收时被放掉；这是强制的，不是"看调用方自觉"。
 *  3. **答案原子**：调用码与具名拒绝来自同一次加锁调用，由同一个写点写进调用方给的 refusalOut
 *     （长度 ≥ 1 的 String[]）。没有"再单独问一次 refusal(handle)"的窗口。
 *  4. **数组边界在动手之前校验**：短数组就是 bad-parameters，且**不执行任何动作**。
 */
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <jni.h>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

#include "jni/backend_api.hpp"
#include "jni/handle_registry.hpp"

namespace ghostlock::jni {
    namespace wrapper {
        /** 把一条名字拷进定长缓冲（截断而不是溢出）。 */
        inline void copy_name(char *out, const std::string_view name) noexcept {
            const std::size_t bytes = name.size() < 127U ? name.size() : 127U;
            std::memcpy(out, name.data(), bytes);
            out[bytes] = '\0';
        }

        /** 数组是否至少有这么长（够只是前提，不是动作）。 */
        [[nodiscard]] inline bool has_room(JNIEnv *env, jlongArray array,
                                          const std::size_t need) noexcept {
            return array != nullptr && env->GetArrayLength(array) >= static_cast<jsize>(need);
        }

        /** 把 jlongArray 读进定长缓冲；短了就回答 false。 */
        [[nodiscard]] inline bool read_longs(JNIEnv *env, jlongArray array, jlong *out,
                                             const std::size_t need) noexcept {
            if (array == nullptr || out == nullptr) {
                return false;
            }
            if (env->GetArrayLength(array) < static_cast<jsize>(need)) {
                return false;
            }
            env->GetLongArrayRegion(array, 0, static_cast<jsize>(need), out);
            return env->ExceptionCheck() == JNI_FALSE;
        }

        /** 把若干 long 写进调用方给的数组。 */
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

        /** 造一个 String[]。 */
        [[nodiscard]] inline jobjectArray make_string_array(JNIEnv *env, const char *const *names,
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
         *  把答案（调用码 + 具名拒绝）一次写完：两者同源、同时写，调用方读到的永远是一对。
         *
         *  @param refusal_out 调用方给的 String[]，长度 ≥ 1；成功时写入空串（可以为 null）。
         *  @return 调用码，直接交给 Kotlin。
         */
        [[nodiscard]] inline jint answer(JNIEnv *env, const CallCode code, const char *const refusal,
                                         jobjectArray refusal_out) noexcept {
            if (refusal_out != nullptr && env->GetArrayLength(refusal_out) >= 1) {
                jstring text = env->NewStringUTF(refusal == nullptr ? "" : refusal);
                env->SetObjectArrayElement(refusal_out, 0, text);
                env->DeleteLocalRef(text);
            }
            return static_cast<jint>(code);
        }

        /** 旧名字：句柄表已抽到 handle_registry.hpp（JNI-free，可宿主测试）。 */
        template <class Backend>
        using Registry = ghostlock::jni::HandleRegistry<Backend>;

    }  // namespace wrapper
}  // namespace ghostlock::jni
