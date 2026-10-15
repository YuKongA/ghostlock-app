/** @file jni/native_43284.cpp
 *  libghostlock43284.so：43284 的 JNI 函数面（用户裁定 2026-10-14）。
 *
 *  与 43499 那个库同构：类型化参数进、调用码出、具名拒绝与调用码**同一次**取到并一起写回 refusalOut。
 *
 *  这个库多一条纪律（审查第 4 点）：**字节结果在会话锁内复制**。镜像与载荷的来源在会话状态里，
 *  先拷进本地 vector，出锁之后才造 Java 数组 —— 并发的 destroy 因此不会让源失效。
 */
#include <cstddef>
#include <cstdint>
#include <jni.h>
#include <string_view>
#include <vector>

#include "jni/api_43284.hpp"
#include "jni/marshal_43284.hpp"
#include "jni/wrapper_support.hpp"

namespace {
    using ghostlock::jni::Backend43284;
    using ghostlock::jni::CallCode;
    using ghostlock::jni::kBlockWidth;
    using ghostlock::jni::marshal::cve43284::Hook43284Layout;
    using ghostlock::jni::marshal::cve43284::Initialize43284Layout;
    using ghostlock::jni::marshal::cve43284::Snapshot43284Layout;
    using ghostlock::jni::wrapper::answer;
    using ghostlock::jni::wrapper::has_room;
    using ghostlock::jni::wrapper::read_longs;
    using ghostlock::jni::wrapper::write_longs;

    ghostlock::jni::wrapper::Registry<Backend43284> &registry() noexcept {
        static ghostlock::jni::wrapper::Registry<Backend43284> table;
        return table;
    }

    /** 读一个 Java 字节数组（路径、模块名）。 */
    [[nodiscard]] bool read_text(JNIEnv *env, jbyteArray array, std::string_view &out,
                                 std::vector<jbyte> &storage) noexcept {
        if (array == nullptr) {
            return false;
        }
        const jsize length = env->GetArrayLength(array);
        storage.resize(static_cast<std::size_t>(length));
        if (length > 0) {
            env->GetByteArrayRegion(array, 0, length, storage.data());
            if (env->ExceptionCheck() != JNI_FALSE) {
                return false;
            }
        }
        out = std::string_view(reinterpret_cast<const char *>(storage.data()),
                               static_cast<std::size_t>(length));
        return true;
    }

    /** 用一份已拷好的字节造 Java 数组（在锁外调用）。 */
    [[nodiscard]] jbyteArray make_bytes(JNIEnv *env, const std::vector<std::byte> &bytes) noexcept {
        if (bytes.empty()) {
            return nullptr;
        }
        jbyteArray out = env->NewByteArray(static_cast<jsize>(bytes.size()));
        if (out == nullptr) {
            return nullptr;
        }
        env->SetByteArrayRegion(out, 0, static_cast<jsize>(bytes.size()),
                                reinterpret_cast<const jbyte *>(bytes.data()));
        return out;
    }
}  // namespace

extern "C" {
/** 建一个会话。@return 句柄（> 0）。 */
JNIEXPORT jlong JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_create(JNIEnv *, jclass) {
    return static_cast<jlong>(registry().create());
}

/** @param params 至少 Initialize43284Layout::count 个 long。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_initialize(
        JNIEnv *env, jclass, jlong handle, jlongArray params, jobjectArray refusal_out) {
    jlong values[Initialize43284Layout::count] = {};
    if (!read_longs(env, params, values, Initialize43284Layout::count)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:params-array", refusal_out);
    }
    const ghostlock::jni::Initialize43284 parsed = ghostlock::jni::marshal::cve43284::parse_initialize(values);
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle), [&parsed](Backend43284 &backend) {
        return backend.initialize(parsed);
    });
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

/** @param out 至少 Snapshot43284Layout::count 个 long。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_snapshot(
        JNIEnv *env, jclass, jlong handle, jlongArray out, jobjectArray refusal_out) {
    if (!has_room(env, out, Snapshot43284Layout::count)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:out-array", refusal_out);
    }
    ghostlock::jni::Snapshot43284 snapshot{};
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle), [&snapshot](Backend43284 &backend) {
        return backend.snapshot(snapshot);
    });
    if (outcome.code == CallCode::Ok) {
        jlong values[Snapshot43284Layout::count] = {};
        ghostlock::jni::marshal::cve43284::fill_snapshot(values, snapshot);
        static_cast<void>(write_longs(env, out, values, Snapshot43284Layout::count));
    }
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

/** 绑定一个角色到一条路径。@param out 至少 1 个 long：票据。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_resolveTarget(
        JNIEnv *env, jclass, jlong handle, jint role, jbyteArray path, jlongArray out,
        jobjectArray refusal_out) {
    if (!has_room(env, out, 1U)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:out-array", refusal_out);
    }
    std::string_view path_text{};
    std::vector<jbyte> storage{};
    if (!read_text(env, path, path_text, storage)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:path-array", refusal_out);
    }
    std::uint64_t ticket = 0U;
    const auto outcome =
        registry().with_answer(static_cast<std::uint64_t>(handle), [role, path_text, &ticket](Backend43284 &backend) {
            return backend.resolve_target(
                ghostlock::jni::target_role_of(static_cast<std::uint64_t>(role)), path_text, ticket);
        });
    if (outcome.code == CallCode::Ok) {
        const jlong written = static_cast<jlong>(ticket);
        static_cast<void>(write_longs(env, out, &written, 1U));
    }
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

/** 写一个十六字节块（宽不是十六一律拒绝，不写一部分）。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_writeBlock(
        JNIEnv *env, jclass, jlong handle, jlong ticket, jlong offset, jbyteArray block,
        jobjectArray refusal_out) {
    if (block == nullptr || env->GetArrayLength(block) != static_cast<jsize>(kBlockWidth)) {
        return answer(env, CallCode::BadParameters, "bad-width", refusal_out);
    }
    std::byte buffer[kBlockWidth] = {};
    env->GetByteArrayRegion(block, 0, static_cast<jsize>(kBlockWidth),
                            reinterpret_cast<jbyte *>(buffer));
    if (env->ExceptionCheck() != JNI_FALSE) {
        return answer(env, CallCode::BadParameters, "bad-parameters:block-array", refusal_out);
    }
    const auto outcome =
        registry().with_answer(static_cast<std::uint64_t>(handle), [ticket, offset, &buffer](Backend43284 &backend) {
            return backend.write_block(static_cast<std::uint64_t>(ticket),
                                       static_cast<std::uint64_t>(offset),
                                       std::span<const std::byte>(buffer, kBlockWidth));
        });
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

/** 读一个十六字节块进调用方的数组。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_readBlock(
        JNIEnv *env, jclass, jlong handle, jlong ticket, jlong offset, jbyteArray out,
        jobjectArray refusal_out) {
    if (out == nullptr || env->GetArrayLength(out) != static_cast<jsize>(kBlockWidth)) {
        return answer(env, CallCode::BadParameters, "bad-width", refusal_out);
    }
    std::byte buffer[kBlockWidth] = {};
    const auto outcome =
        registry().with_answer(static_cast<std::uint64_t>(handle), [ticket, offset, &buffer](Backend43284 &backend) {
            return backend.read_block(static_cast<std::uint64_t>(ticket),
                                      static_cast<std::uint64_t>(offset),
                                      std::span<std::byte>(buffer, kBlockWidth));
        });
    if (outcome.code == CallCode::Ok) {
        env->SetByteArrayRegion(out, 0, static_cast<jsize>(kBlockWidth),
                                reinterpret_cast<const jbyte *>(buffer));
    }
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

/** 双 fork 触发。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_trigger(
        JNIEnv *env, jclass, jlong handle, jlong delay_ms, jobjectArray refusal_out) {
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle), [delay_ms](Backend43284 &backend) {
        return backend.trigger(static_cast<std::uint64_t>(delay_ms));
    });
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

/** 等终态。@param out 至少 1 个 long：0 failure / 1 success / 2 inflight。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_waitTerminal(
        JNIEnv *env, jclass, jlong handle, jlong budget_ms, jlong poll_ms, jbyteArray module_name,
        jlongArray out, jobjectArray refusal_out) {
    if (!has_room(env, out, 1U)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:out-array", refusal_out);
    }
    std::string_view module_text{};
    std::vector<jbyte> storage{};
    if (!read_text(env, module_name, module_text, storage)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:module-array", refusal_out);
    }
    ghostlock::jni::WaitTerminal43284 result{};
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                                 [budget_ms, poll_ms, module_text, &result](Backend43284 &backend) {
            return backend.wait_terminal(static_cast<std::uint64_t>(budget_ms),
                                         static_cast<std::uint64_t>(poll_ms), module_text, result);
        });
    if (outcome.code == CallCode::Ok || outcome.code == CallCode::InFlight ||
        outcome.code == CallCode::Failed) {
        const jlong written = static_cast<jlong>(result.terminal);
        static_cast<void>(write_longs(env, out, &written, 1U));
    }
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

/** @return 本构建自带的字节；失败时 null（名字在 refusalOut 里）。 */
JNIEXPORT jbyteArray JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_embeddedImage(
        JNIEnv *env, jclass, jlong handle, jint role, jobjectArray refusal_out) {
    std::vector<std::byte> copied{};
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle), [role, &copied](Backend43284 &backend) {
        std::span<const std::byte> image{};
        const CallCode code = backend.embedded_image(static_cast<std::uint64_t>(role), image);
        if (code == CallCode::Ok) {
            /* 锁内拷贝：出锁之后源可能已被回收。 */
            copied.assign(image.begin(), image.end());
        }
        return code;
    });
    static_cast<void>(answer(env, outcome.code, outcome.refusal, refusal_out));
    return outcome.code == CallCode::Ok ? make_bytes(env, copied) : nullptr;
}

/** @return 钩子要写的载荷；失败时 null（名字在 refusalOut 里）。 */
JNIEXPORT jbyteArray JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_hookPayload(
        JNIEnv *env, jclass, jlong handle, jobjectArray refusal_out) {
    std::vector<std::byte> copied{};
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle), [&copied](Backend43284 &backend) {
        ghostlock::jni::HookWords43284 words{};
        const CallCode code = backend.hook_words(words);
        if (code == CallCode::Ok) {
            copied.assign(words.payload.begin(), words.payload.end());
        }
        return code;
    });
    static_cast<void>(answer(env, outcome.code, outcome.refusal, refusal_out));
    return outcome.code == CallCode::Ok ? make_bytes(env, copied) : nullptr;
}

/** 钩子的四个数字。@param out 至少 Hook43284Layout::count 个 long。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_hookWords(
        JNIEnv *env, jclass, jlong handle, jlongArray out, jobjectArray refusal_out) {
    if (!has_room(env, out, Hook43284Layout::count)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:out-array", refusal_out);
    }
    ghostlock::jni::HookWords43284 words{};
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle), [&words](Backend43284 &backend) {
        return backend.hook_words(words);
    });
    if (outcome.code == CallCode::Ok) {
        jlong values[Hook43284Layout::count] = {};
        ghostlock::jni::marshal::cve43284::fill_hook(values, words);
        static_cast<void>(write_longs(env, out, values, Hook43284Layout::count));
    }
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

/** 关掉这个会话（幂等）。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_shutdown(
        JNIEnv *env, jclass, jlong handle, jobjectArray refusal_out) {
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle), [](Backend43284 &backend) {
        return backend.shutdown();
    });
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

/** 回收这个会话：内部先 shutdown 再清状态；之后句柄作废。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_destroy(
        JNIEnv *env, jclass, jlong handle, jobjectArray refusal_out) {
    const bool retired = registry().retire(static_cast<std::uint64_t>(handle));
    return answer(env, retired ? CallCode::Ok : CallCode::StaleHandle,
                  retired ? "" : "stale-handle", refusal_out);
}

/** @return snapshot 的数组布局。 */
JNIEXPORT jobjectArray JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_snapshotFields(
        JNIEnv *env, jclass) {
    return ghostlock::jni::wrapper::make_string_array(env, Snapshot43284Layout::names,
                                                      Snapshot43284Layout::count);
}

/** @return initialize 的数组布局。 */
JNIEXPORT jobjectArray JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_initializeFields(
        JNIEnv *env, jclass) {
    return ghostlock::jni::wrapper::make_string_array(env, Initialize43284Layout::names,
                                                      Initialize43284Layout::count);
}

/** @return hookWords 的数组布局。 */
JNIEXPORT jobjectArray JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_hookFields(
        JNIEnv *env, jclass) {
    return ghostlock::jni::wrapper::make_string_array(env, Hook43284Layout::names,
                                                      Hook43284Layout::count);
}
}  // extern "C"
