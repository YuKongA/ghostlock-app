/** @file jni/native_43284.cpp
 *  libghostlock43284.so：43284 的 JNI 函数面（用户裁定 2026-10-14）。
 *
 *  与 43499 那个库同构：类型化参数进、调用码出、失败原因由 refusal(handle) 取、数组布局自描述。
 *  字节只出现在真正是字节的地方（路径、十六字节块、镜像、载荷）—— 那里本来就是字节。
 */
#include <cstddef>
#include <cstdint>
#include <jni.h>

#include "jni/api_43284.hpp"
#include "jni/wrapper_support.hpp"

namespace {
    using ghostlock::jni::Backend43284;
    using ghostlock::jni::CallCode;
    using ghostlock::jni::kBlockWidth;
    using ghostlock::jni::wrapper::read_longs;
    using ghostlock::jni::wrapper::write_longs;

    ghostlock::jni::wrapper::Registry<Backend43284> &registry() noexcept {
        static ghostlock::jni::wrapper::Registry<Backend43284> table;
        return table;
    }

    [[nodiscard]] jint code(const CallCode value) noexcept { return static_cast<jint>(value); }

    constexpr const char *const kSnapshotFields[] = {
        "phase", "flags", "width", "token", "role", "sequence",
    };
    constexpr std::size_t kSnapshotFieldCount = sizeof(kSnapshotFields) / sizeof(kSnapshotFields[0]);

    constexpr const char *const kInitializeFields[] = {
        "trigger_delay_ms", "wait_budget_ms", "wait_poll_ms", "carrier_base",
    };
    constexpr std::size_t kInitializeFieldCount =
        sizeof(kInitializeFields) / sizeof(kInitializeFields[0]);

    constexpr const char *const kHookFields[] = {
        "branch_word", "displaced_word", "site_offset", "landing_offset",
    };
    constexpr std::size_t kHookFieldCount = sizeof(kHookFields) / sizeof(kHookFields[0]);

    void fill_snapshot(jlong *out, const ghostlock::jni::Snapshot43284 &snapshot) noexcept {
        out[0] = static_cast<jlong>(snapshot.phase);
        out[1] = static_cast<jlong>(snapshot.flags);
        out[2] = static_cast<jlong>(snapshot.width);
        out[3] = static_cast<jlong>(snapshot.facts.token);
        out[4] = static_cast<jlong>(snapshot.facts.role);
        out[5] = static_cast<jlong>(snapshot.facts.sequence);
    }

    /** 把 path/模块名一类字节读成 std::string_view（长度由 Java 数组给）。 */
    [[nodiscard]] bool read_bytes(JNIEnv *env, jbyteArray array, std::string_view &out,
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
}  // namespace

extern "C" {
/** 建一个会话。@return 句柄（> 0）。 */
JNIEXPORT jlong JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_create(JNIEnv *, jclass) {
    return static_cast<jlong>(registry().create());
}

/** @param params 至少 4 个 long（见 initializeFields()）。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_initialize(
        JNIEnv *env, jclass, jlong handle, jlongArray params) {
    jlong values[kInitializeFieldCount] = {};
    if (!read_longs(env, params, values, kInitializeFieldCount)) {
        return code(CallCode::BadParameters);
    }
    ghostlock::jni::Initialize43284 parsed{};
    parsed.trigger_delay_ms = static_cast<std::uint64_t>(values[0]);
    parsed.wait_budget_ms = static_cast<std::uint64_t>(values[1]);
    parsed.wait_poll_ms = static_cast<std::uint64_t>(values[2]);
    parsed.carrier_base = static_cast<std::uint64_t>(values[3]);
    return code(registry().with(handle, [&parsed](Backend43284 &backend) {
        return backend.initialize(parsed);
    }));
}

/** @param out 至少 6 个 long（见 snapshotFields()）。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_snapshot(
        JNIEnv *env, jclass, jlong handle, jlongArray out) {
    if (out == nullptr || env->GetArrayLength(out) < static_cast<jsize>(kSnapshotFieldCount)) {
        return code(CallCode::BadParameters);
    }
    ghostlock::jni::Snapshot43284 snapshot{};
    const CallCode answer = registry().with(handle, [&snapshot](Backend43284 &backend) {
        return backend.snapshot(snapshot);
    });
    if (answer == CallCode::Ok) {
        jlong values[kSnapshotFieldCount] = {};
        fill_snapshot(values, snapshot);
        if (!write_longs(env, out, values, kSnapshotFieldCount)) {
            return code(CallCode::BadParameters);
        }
    }
    return code(answer);
}

/** 绑定一个角色到一条路径。@param out 一个 long：token。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_resolveTarget(
        JNIEnv *env, jclass, jlong handle, jint role, jbyteArray path, jlongArray out) {
    if (out == nullptr || env->GetArrayLength(out) < 1) {
        return code(CallCode::BadParameters);
    }
    std::string_view path_text{};
    std::vector<jbyte> storage{};
    if (!read_bytes(env, path, path_text, storage)) {
        return code(CallCode::BadParameters);
    }
    std::uint64_t token = 0U;
    const CallCode answer = registry().with(handle, [role, path_text, &token](Backend43284 &backend) {
        return backend.resolve_target(ghostlock::jni::target_role_of(static_cast<std::uint64_t>(role)),
                                      path_text, token);
    });
    if (answer == CallCode::Ok) {
        const jlong written = static_cast<jlong>(token);
        if (!write_longs(env, out, &written, 1U)) {
            return code(CallCode::BadParameters);
        }
    }
    return code(answer);
}

/** 写一个十六字节块。块宽不是十六字节就是 BadParameters（不是"写一部分"）。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_writeBlock(
        JNIEnv *env, jclass, jlong handle, jlong token, jlong offset, jbyteArray block) {
    if (block == nullptr || env->GetArrayLength(block) != static_cast<jsize>(kBlockWidth)) {
        return code(CallCode::BadParameters);
    }
    std::byte buffer[kBlockWidth] = {};
    env->GetByteArrayRegion(block, 0, static_cast<jsize>(kBlockWidth),
                            reinterpret_cast<jbyte *>(buffer));
    if (env->ExceptionCheck() != JNI_FALSE) {
        return code(CallCode::BadParameters);
    }
    return code(registry().with(handle, [token, offset, &buffer](Backend43284 &backend) {
        return backend.write_block(static_cast<std::uint64_t>(token),
                                   static_cast<std::uint64_t>(offset),
                                   std::span<const std::byte>(buffer, kBlockWidth));
    }));
}

/** 读一个十六字节块进调用方的数组。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_readBlock(
        JNIEnv *env, jclass, jlong handle, jlong token, jlong offset, jbyteArray out) {
    if (out == nullptr || env->GetArrayLength(out) != static_cast<jsize>(kBlockWidth)) {
        return code(CallCode::BadParameters);
    }
    std::byte buffer[kBlockWidth] = {};
    const CallCode answer = registry().with(handle, [token, offset, &buffer](Backend43284 &backend) {
        return backend.read_block(static_cast<std::uint64_t>(token),
                                  static_cast<std::uint64_t>(offset),
                                  std::span<std::byte>(buffer, kBlockWidth));
    });
    if (answer == CallCode::Ok) {
        env->SetByteArrayRegion(out, 0, static_cast<jsize>(kBlockWidth),
                                reinterpret_cast<const jbyte *>(buffer));
    }
    return code(answer);
}

/** 双 fork 触发。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_trigger(
        JNIEnv *, jclass, jlong handle, jlong delay_ms) {
    return code(registry().with(handle, [delay_ms](Backend43284 &backend) {
        return backend.trigger(static_cast<std::uint64_t>(delay_ms));
    }));
}

/** 等终态。@param out 一个 long：TerminalState（0 failure / 1 success / 2 inflight）。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_waitTerminal(
        JNIEnv *env, jclass, jlong handle, jlong budget_ms, jlong poll_ms, jbyteArray module_name,
        jlongArray out) {
    if (out == nullptr || env->GetArrayLength(out) < 1) {
        return code(CallCode::BadParameters);
    }
    std::string_view module_text{};
    std::vector<jbyte> storage{};
    if (!read_bytes(env, module_name, module_text, storage)) {
        return code(CallCode::BadParameters);
    }
    ghostlock::jni::WaitTerminal43284 result{};
    const CallCode answer = registry().with(
        handle, [budget_ms, poll_ms, module_text, &result](Backend43284 &backend) {
            return backend.wait_terminal(static_cast<std::uint64_t>(budget_ms),
                                         static_cast<std::uint64_t>(poll_ms), module_text, result);
        });
    if (answer == CallCode::Ok || answer == CallCode::InFlight || answer == CallCode::Failed) {
        const jlong written = static_cast<jlong>(result.terminal);
        if (!write_longs(env, out, &written, 1U)) {
            return code(CallCode::BadParameters);
        }
    }
    return code(answer);
}

/** @return 本构建自带的字节；失败时 null（原因见 refusal）。 */
JNIEXPORT jbyteArray JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_embeddedImage(
        JNIEnv *env, jclass, jlong handle, jint role) {
    std::span<const std::byte> image{};
    const CallCode answer = registry().with(handle, [role, &image](Backend43284 &backend) {
        return backend.embedded_image(static_cast<std::uint64_t>(role), image);
    });
    if (answer != CallCode::Ok || image.empty()) {
        return nullptr;
    }
    jbyteArray out = env->NewByteArray(static_cast<jsize>(image.size()));
    if (out == nullptr) {
        return nullptr;
    }
    env->SetByteArrayRegion(out, 0, static_cast<jsize>(image.size()),
                            reinterpret_cast<const jbyte *>(image.data()));
    return out;
}

/** 钩子的四个数字。@param out 至少 4 个 long（见 hookFields()）。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_hookWords(
        JNIEnv *env, jclass, jlong handle, jlongArray out) {
    if (out == nullptr || env->GetArrayLength(out) < static_cast<jsize>(kHookFieldCount)) {
        return code(CallCode::BadParameters);
    }
    ghostlock::jni::HookWords43284 words{};
    const CallCode answer = registry().with(handle, [&words](Backend43284 &backend) {
        return backend.hook_words(words);
    });
    if (answer == CallCode::Ok) {
        const jlong values[kHookFieldCount] = {
            static_cast<jlong>(words.branch_word),
            static_cast<jlong>(words.displaced_word),
            static_cast<jlong>(words.site_offset),
            static_cast<jlong>(words.landing_offset),
        };
        if (!write_longs(env, out, values, kHookFieldCount)) {
            return code(CallCode::BadParameters);
        }
    }
    return code(answer);
}

/** @return 钩子要写的载荷；失败时 null（原因见 refusal）。 */
JNIEXPORT jbyteArray JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_hookPayload(
        JNIEnv *env, jclass, jlong handle) {
    ghostlock::jni::HookWords43284 words{};
    const CallCode answer = registry().with(handle, [&words](Backend43284 &backend) {
        return backend.hook_words(words);
    });
    if (answer != CallCode::Ok || words.payload.empty()) {
        return nullptr;
    }
    jbyteArray out = env->NewByteArray(static_cast<jsize>(words.payload.size()));
    if (out == nullptr) {
        return nullptr;
    }
    env->SetByteArrayRegion(out, 0, static_cast<jsize>(words.payload.size()),
                            reinterpret_cast<const jbyte *>(words.payload.data()));
    return out;
}

/** 关掉这个会话（幂等）。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_shutdown(
        JNIEnv *, jclass, jlong handle) {
    return code(registry().with(handle, [](Backend43284 &backend) { return backend.shutdown(); }));
}

/** 回收这个会话（之后句柄作废）。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_destroy(
        JNIEnv *, jclass, jlong handle) {
    return registry().retire(static_cast<std::uint64_t>(handle)) ? code(CallCode::Ok)
                                                                : code(CallCode::StaleHandle);
}

/** @return 这个句柄最后一条具名拒绝；无效句柄或没有拒绝时是空串。 */
JNIEXPORT jstring JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_refusal(
        JNIEnv *env, jclass, jlong handle) {
    char text[128] = {};
    registry().with(handle, [&text](Backend43284 &backend) {
        const char *const name = backend.refusal();
        for (std::size_t index = 0U; index + 1U < sizeof(text) && name[index] != '\0'; ++index) {
            text[index] = name[index];
        }
        return CallCode::Ok;
    });
    return env->NewStringUTF(text);
}

/** @return snapshot 的数组布局。 */
JNIEXPORT jobjectArray JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_snapshotFields(
        JNIEnv *env, jclass) {
    return ghostlock::jni::wrapper::make_string_array(env, kSnapshotFields, kSnapshotFieldCount);
}

/** @return initialize 的数组布局。 */
JNIEXPORT jobjectArray JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_initializeFields(
        JNIEnv *env, jclass) {
    return ghostlock::jni::wrapper::make_string_array(env, kInitializeFields, kInitializeFieldCount);
}

/** @return hookWords 的数组布局。 */
JNIEXPORT jobjectArray JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_hookFields(
        JNIEnv *env, jclass) {
    return ghostlock::jni::wrapper::make_string_array(env, kHookFields, kHookFieldCount);
}
}  // extern "C"
