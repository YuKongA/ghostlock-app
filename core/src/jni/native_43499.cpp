/** @file jni/native_43499.cpp
 *  libghostlock43499.so：43499 的 JNI 函数面（用户裁定 2026-10-14）。
 *
 *  每个函数都是普通函数：参数是数字/布尔/数组，返回值是调用码；具名拒绝与调用码来自**同一次**
 *  加锁调用（with_answer），并一起写进调用方给的 refusalOut（长度 ≥ 1 的 String[]）。
 *
 *  三条纪律（按审查要求写死在这里）：
 *  1. **先校验后动手**：输出数组的长度在调用 backend 之前检查完；短数组 = bad-parameters，
 *     并且**不执行任何动作**。
 *  2. **字节结果在锁内复制**：本 backend 今天没有字节返回，规则照旧适用于 43284 那个库。
 *  3. **布局只有一份**：名字与下标都来自 marshal_43499.hpp 的名字表，本文件不写字面下标。
 */
#include <cstddef>
#include <cstdint>
#include <jni.h>

#include "jni/api_43499.hpp"
#include "jni/marshal_43499.hpp"
#include "jni/wrapper_support.hpp"

namespace {
    using ghostlock::jni::Backend43499;
    using ghostlock::jni::CallCode;
    using ghostlock::jni::marshal::cve43499::Initialize43499Layout;
    using ghostlock::jni::marshal::cve43499::Snapshot43499Layout;
    using ghostlock::jni::wrapper::answer;
    using ghostlock::jni::wrapper::has_room;
    using ghostlock::jni::wrapper::read_longs;
    using ghostlock::jni::wrapper::write_longs;

    ghostlock::jni::wrapper::Registry<Backend43499> &registry() noexcept {
        static ghostlock::jni::wrapper::Registry<Backend43499> table;
        return table;
    }
}  // namespace

extern "C" {
/** 建一个会话。@return 句柄（> 0）。 */
JNIEXPORT jlong JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_create(JNIEnv *, jclass) {
    return static_cast<jlong>(registry().create());
}

/** @param params 至少 Initialize43499Layout::count 个 long；@param refusal_out 至少 1 个 String。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_initialize(
        JNIEnv *env, jclass, jlong handle, jlongArray params, jobjectArray refusal_out) {
    jlong values[Initialize43499Layout::count] = {};
    if (!read_longs(env, params, values, Initialize43499Layout::count)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:params-array", refusal_out);
    }
    const ghostlock::jni::Initialize43499 parsed = ghostlock::jni::marshal::cve43499::parse_initialize(values);
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle), [&parsed](Backend43499 &backend) {
        return backend.initialize(parsed);
    });
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

/** @param out 至少 Snapshot43499Layout::count 个 long。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_snapshot(
        JNIEnv *env, jclass, jlong handle, jlongArray out, jobjectArray refusal_out) {
    if (!has_room(env, out, Snapshot43499Layout::count)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:out-array", refusal_out);
    }
    ghostlock::jni::Snapshot43499 snapshot{};
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle), [&snapshot](Backend43499 &backend) {
        return backend.snapshot(snapshot);
    });
    if (outcome.code == CallCode::Ok) {
        jlong values[Snapshot43499Layout::count] = {};
        ghostlock::jni::marshal::cve43499::fill_snapshot(values, snapshot);
        static_cast<void>(write_longs(env, out, values, Snapshot43499Layout::count));
    }
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

/** 跑准备。@param out 同 snapshot。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_prepare(
        JNIEnv *env, jclass, jlong handle, jlongArray out, jobjectArray refusal_out) {
    if (!has_room(env, out, Snapshot43499Layout::count)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:out-array", refusal_out);
    }
    ghostlock::jni::Snapshot43499 snapshot{};
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle), [&snapshot](Backend43499 &backend) {
        return backend.prepare(snapshot);
    });
    if (outcome.code == CallCode::Ok) {
        jlong values[Snapshot43499Layout::count] = {};
        ghostlock::jni::marshal::cve43499::fill_snapshot(values, snapshot);
        static_cast<void>(write_longs(env, out, values, Snapshot43499Layout::count));
    }
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

/** 跑搜索。@param out 同 snapshot。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_discover(
        JNIEnv *env, jclass, jlong handle, jlongArray out, jobjectArray refusal_out) {
    if (!has_room(env, out, Snapshot43499Layout::count)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:out-array", refusal_out);
    }
    ghostlock::jni::Snapshot43499 snapshot{};
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle), [&snapshot](Backend43499 &backend) {
        return backend.discover(snapshot);
    });
    if (outcome.code == CallCode::Ok) {
        jlong values[Snapshot43499Layout::count] = {};
        ghostlock::jni::marshal::cve43499::fill_snapshot(values, snapshot);
        static_cast<void>(write_longs(env, out, values, Snapshot43499Layout::count));
    }
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

/** fork 受害者。@param out 同 snapshot。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_spawnVictim(
        JNIEnv *env, jclass, jlong handle, jlongArray out, jobjectArray refusal_out) {
    if (!has_room(env, out, Snapshot43499Layout::count)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:out-array", refusal_out);
    }
    ghostlock::jni::Snapshot43499 snapshot{};
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle), [&snapshot](Backend43499 &backend) {
        return backend.spawn_victim(snapshot);
    });
    if (outcome.code == CallCode::Ok) {
        jlong values[Snapshot43499Layout::count] = {};
        ghostlock::jni::marshal::cve43499::fill_snapshot(values, snapshot);
        static_cast<void>(write_longs(env, out, values, Snapshot43499Layout::count));
    }
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

/** 问受害者是什么：kind 0 = uid，1 = comm。@param out 至少 1 个 long。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_victimProbe(
        JNIEnv *env, jclass, jlong handle, jint kind, jlongArray out, jobjectArray refusal_out) {
    if (!has_room(env, out, 1U)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:out-array", refusal_out);
    }
    std::uint64_t value = 0U;
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle), [kind, &value](Backend43499 &backend) {
        return backend.victim_probe(static_cast<std::uint64_t>(kind), value);
    });
    if (outcome.code == CallCode::Ok) {
        const jlong written = static_cast<jlong>(value);
        static_cast<void>(write_longs(env, out, &written, 1U));
    }
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

/** 落一个字。form：1 保留清零 / 2 叶子清零 / 3 写值保留。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_writeWord(
        JNIEnv *env, jclass, jlong handle, jlong address, jlong value, jint form, jlong offset,
        jboolean value_given, jobjectArray refusal_out) {
    ghostlock::jni::WriteWord43499 request{};
    request.address = static_cast<std::uint64_t>(address);
    request.value = static_cast<std::uint64_t>(value);
    request.form = ghostlock::jni::write_form_of(static_cast<std::uint64_t>(form));
    request.offset = static_cast<std::uint64_t>(offset);
    request.value_given = value_given == JNI_TRUE;
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle), [&request](Backend43499 &backend) {
        return backend.write_word(request);
    });
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

/** 读一个字。@param out 至少 1 个 long。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_readWord(
        JNIEnv *env, jclass, jlong handle, jlong address, jlongArray out, jobjectArray refusal_out) {
    if (!has_room(env, out, 1U)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:out-array", refusal_out);
    }
    std::uint64_t value = 0U;
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle), [address, &value](Backend43499 &backend) {
        return backend.read_word(static_cast<std::uint64_t>(address), value);
    });
    if (outcome.code == CallCode::Ok) {
        const jlong written = static_cast<jlong>(value);
        static_cast<void>(write_longs(env, out, &written, 1U));
    }
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

/** 关掉这个会话（幂等）。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_shutdown(
        JNIEnv *env, jclass, jlong handle, jobjectArray refusal_out) {
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle), [](Backend43499 &backend) {
        return backend.shutdown();
    });
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

/** 回收这个会话：内部先 shutdown 再清状态；之后句柄作废。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_destroy(
        JNIEnv *env, jclass, jlong handle, jobjectArray refusal_out) {
    const bool retired = registry().retire(static_cast<std::uint64_t>(handle));
    return answer(env, retired ? CallCode::Ok : CallCode::StaleHandle,
                  retired ? "" : "stale-handle", refusal_out);
}

/** @return snapshot 的数组布局。 */
JNIEXPORT jobjectArray JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_snapshotFields(
        JNIEnv *env, jclass) {
    return ghostlock::jni::wrapper::make_string_array(env, Snapshot43499Layout::names,
                                                      Snapshot43499Layout::count);
}

/** @return initialize 的数组布局。 */
JNIEXPORT jobjectArray JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_initializeFields(
        JNIEnv *env, jclass) {
    return ghostlock::jni::wrapper::make_string_array(env, Initialize43499Layout::names,
                                                      Initialize43499Layout::count);
}
}  // extern "C"
