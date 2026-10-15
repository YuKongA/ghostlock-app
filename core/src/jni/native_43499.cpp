/** @file jni/native_43499.cpp
 *  libghostlock43499.so：43499 的 JNI 函数面（用户裁定 2026-10-14；D2 参数装配 2026-10-15）。
 *
 *  参数不再是位置数组：每个函数收 Kotlin 的 **DTO 对象**（D1 单表生成，@JvmField 字段），由同一张表
 *  生成的读取器 read<Group> 逐字段复制到 native Values。读取失败是具名的：bad-parameters:<字段>。
 *
 *  三条纪律（审查要求）：
 *  1. **先校验后动手**：输出数组长度在调用 backend 之前检查，短数组 = bad-parameters 且零动作；
 *  2. **答案成对**：调用码与具名拒绝来自同一次加锁调用，一起写回 refusalOut；
 *  3. **布局只有一份**：结果数组的名字表来自 marshal_43499.hpp（同表驱动的名字，无字面下标）。
 */
#include <string>
#include "contract/kernel_access.hpp"
#include "jni/api_43499_device.hpp"
#include <cstddef>
#include <cstdint>
#include <jni.h>

#include "jni/api_43499.hpp"
#include "jni/attack_contract_reader.hpp"
#include "jni/marshal_43499.hpp"
#include "jni/wrapper_support.hpp"

namespace {
    using ghostlock::jni::Backend43499;
    using ghostlock::jni::CallCode;
    using ghostlock::jni::SessionParams43499;
    using ghostlock::jni::contract::ReadFailure;
    using ghostlock::jni::marshal::cve43499::Snapshot43499Layout;
    using ghostlock::jni::wrapper::answer;
    using ghostlock::jni::wrapper::has_room;
    using ghostlock::jni::wrapper::write_longs;

    ghostlock::jni::wrapper::Registry<Backend43499> &registry() noexcept {
        static ghostlock::jni::wrapper::Registry<Backend43499> table;
        return table;
    }

    /** 读取器失败时，把 backend 的具名拒绝装上（名字就是出错的字段）。 */
    template <class Work>
    [[nodiscard]] ghostlock::jni::wrapper::Registry<Backend43499>::Outcome
    refused_read(const ghostlock::jni::wrapper::Registry<Backend43499>::Outcome outcome,
                 Work &&work) noexcept {
        static_cast<void>(work);
        return outcome;
    }

    /** 快照结果写进调用方数组（名字表来自 marshal，单一权威）。 */
    void publish(ghostlock::jni::wrapper::Registry<Backend43499>::Outcome &outcome, JNIEnv *env,
                 const ghostlock::jni::Snapshot43499 &snapshot, jlongArray out) noexcept {
        if (outcome.code != CallCode::Ok) {
            return;
        }
        jlong values[Snapshot43499Layout::count] = {};
        ghostlock::jni::marshal::cve43499::fill_snapshot(values, snapshot);
        static_cast<void>(write_longs(env, out, values, Snapshot43499Layout::count));
    }
}  // namespace

extern "C" {
/** 建一个会话。@return 句柄（> 0）。 */
JNIEXPORT jlong JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_create(JNIEnv *, jclass) {
    /* 会话模板：缝在这里给（43499 的设备实现属于 J7.5，此刻仍为空 ⇒ op-not-wired）。 */
    registry().set_factory([]() noexcept { return Backend43499{}; });
    return static_cast<jlong>(registry().create());
}

/** 初始化：四组 DTO 一起给，缺字段按名拒绝。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_initialize(
        JNIEnv *env, jclass, jlong handle, jobject init, jobject layout, jobject credential,
        jobject targets, jobjectArray refusal_out) {
    SessionParams43499 params{};
    ReadFailure failure{};
    if (!ghostlock::jni::contract::readInit43499(env, init, &params.init, &failure) ||
        !ghostlock::jni::contract::readKernelLayout43499(env, layout, &params.layout, &failure) ||
        !ghostlock::jni::contract::readCredentialLayout43499(env, credential, &params.credential,
                                                             &failure) ||
        !ghostlock::jni::contract::readTargetOffsets43499(env, targets, &params.targets, &failure)) {
        const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                                    [&failure](Backend43499 &backend) {
                                                        return backend.reject_initialize(failure);
                                                    });
        return answer(env, outcome.code, outcome.refusal, refusal_out);
    }
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                                [&params](Backend43499 &backend) {
                                                    return backend.initialize(params);
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
    auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                          [&snapshot](Backend43499 &backend) {
                                              return backend.snapshot(snapshot);
                                          });
    publish(outcome, env, snapshot, out);
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

/** 跑准备。@param request Prepare43499 DTO；@param out 同 snapshot。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_prepare(
        JNIEnv *env, jclass, jlong handle, jobject request, jlongArray out, jobjectArray refusal_out) {
    if (!has_room(env, out, Snapshot43499Layout::count)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:out-array", refusal_out);
    }
    ghostlock::jni::contract::Prepare43499Values values{};
    ReadFailure failure{};
    if (!ghostlock::jni::contract::readPrepare43499(env, request, &values, &failure)) {
        const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                                    [&failure](Backend43499 &backend) {
                                                        return backend.reject_initialize(failure);
                                                    });
        return answer(env, outcome.code, outcome.refusal, refusal_out);
    }
    ghostlock::jni::Snapshot43499 snapshot{};
    auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                          [&values, &snapshot](Backend43499 &backend) {
                                              return backend.prepare(values, snapshot);
                                          });
    publish(outcome, env, snapshot, out);
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

/** 跑搜索。@param request Discover43499 DTO；@param out 同 snapshot。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_discover(
        JNIEnv *env, jclass, jlong handle, jobject request, jlongArray out, jobjectArray refusal_out) {
    if (!has_room(env, out, Snapshot43499Layout::count)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:out-array", refusal_out);
    }
    ghostlock::jni::contract::Discover43499Values values{};
    ReadFailure failure{};
    if (!ghostlock::jni::contract::readDiscover43499(env, request, &values, &failure)) {
        const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                                    [&failure](Backend43499 &backend) {
                                                        return backend.reject_initialize(failure);
                                                    });
        return answer(env, outcome.code, outcome.refusal, refusal_out);
    }
    ghostlock::jni::Snapshot43499 snapshot{};
    auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                          [&values, &snapshot](Backend43499 &backend) {
                                              return backend.discover(values, snapshot);
                                          });
    publish(outcome, env, snapshot, out);
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

/** fork 受害者。@param out 同 snapshot。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_spawnVictim(
        JNIEnv *env, jclass, jlong handle, jlongArray out, jobjectArray refusal_out) {
    if (!has_room(env, out, Snapshot43499Layout::count)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:out-array", refusal_out);
    }
    ghostlock::jni::Snapshot43499 snapshot{};
    auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                          [&snapshot](Backend43499 &backend) {
                                              return backend.spawn_victim(snapshot);
                                          });
    publish(outcome, env, snapshot, out);
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

/** 受害者收尾（retire / park / handoff）。@param request FinishVictim43499 DTO。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_finishVictim(
        JNIEnv *env, jclass, jlong handle, jobject request, jlongArray out,
        jobjectArray refusal_out) {
    if (!has_room(env, out, Snapshot43499Layout::count)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:out-array", refusal_out);
    }
    ghostlock::jni::contract::FinishVictim43499Values values{};
    ReadFailure failure{};
    if (!ghostlock::jni::contract::readFinishVictim43499(env, request, &values, &failure)) {
        const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                                    [&failure](Backend43499 &backend) {
                                                        return backend.reject_initialize(failure);
                                                    });
        return answer(env, outcome.code, outcome.refusal, refusal_out);
    }
    ghostlock::jni::Snapshot43499 snapshot{};
    auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                          [&values, &snapshot](Backend43499 &backend) {
                                              return backend.finish_victim(values, snapshot);
                                          });
    publish(outcome, env, snapshot, out);
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

/** 问受害者是什么：kind 0 = uid，1 = comm。@param out 至少 1 个 long。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_victimProbe(
        JNIEnv *env, jclass, jlong handle, jint kind, jlongArray out, jobjectArray refusal_out) {
    if (!has_room(env, out, 1U)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:out-array", refusal_out);
    }
    std::uint64_t value = 0U;
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                                [kind, &value](Backend43499 &backend) {
                                                    return backend.victim_probe(
                                                        static_cast<std::uint64_t>(kind), value);
                                                });
    if (outcome.code == CallCode::Ok) {
        const jlong written = static_cast<jlong>(value);
        static_cast<void>(write_longs(env, out, &written, 1U));
    }
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

/** 落一个字。@param request WordWrite43499 DTO（含三代与形状）。 */
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_writeWord(
        JNIEnv *env, jclass, jlong handle, jobject request, jobjectArray refusal_out) {
    ghostlock::jni::contract::WordWrite43499Values values{};
    ReadFailure failure{};
    if (!ghostlock::jni::contract::readWordWrite43499(env, request, &values, &failure)) {
        const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                                    [&failure](Backend43499 &backend) {
                                                        return backend.reject_initialize(failure);
                                                    });
        return answer(env, outcome.code, outcome.refusal, refusal_out);
    }
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                                [&values](Backend43499 &backend) {
                                                    return backend.write_word(values);
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
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                                [address, &value](Backend43499 &backend) {
                                                    return backend.read_word(
                                                        static_cast<std::uint64_t>(address), value);
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
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                                [](Backend43499 &backend) {
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

/** @return snapshot 结果数组的布局。 */
JNIEXPORT jobjectArray JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_snapshotFields(
        JNIEnv *env, jclass) {
    return ghostlock::jni::wrapper::make_string_array(env, Snapshot43499Layout::names,
                                                      Snapshot43499Layout::count);
}
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_makeAccess43499(
        JNIEnv *env, jclass, jlong, jint route, jlongArray out, jobjectArray refusal_out) {
    if (!has_room(env, out, 1U)) {
        return answer(env, ghostlock::jni::CallCode::BadParameters, "bad-parameters:out-array", refusal_out);
    }
    const std::uint64_t made = ghostlock::jni::device_make_access_43499(static_cast<int>(route));
    if (made == 0U) {
        return answer(env, ghostlock::jni::CallCode::Failed, "access-refused", refusal_out);
    }
    const jlong written = static_cast<jlong>(made);
    static_cast<void>(write_longs(env, out, &written, 1U));
    return answer(env, ghostlock::jni::CallCode::Ok, "", refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_destroyAccess43499(
        JNIEnv *env, jclass, jlong, jlong accessId, jobjectArray refusal_out) {
    const bool done = ghostlock::jni::device_destroy_access_43499(static_cast<std::uint64_t>(accessId));
    return answer(env, done ? ghostlock::jni::CallCode::Ok : ghostlock::jni::CallCode::StaleTarget,
                  done ? "" : "stale-target", refusal_out);
}


JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_initialize43499(
        JNIEnv *env, jclass, jlong, jlong accessId, jint route, jlong offset, jboolean offsetSet,
        jobjectArray refusal_out) {
    const int status = ghostlock::jni::device_initialize_43499(
        static_cast<std::uint64_t>(accessId), static_cast<std::uint64_t>(route),
        static_cast<std::uint64_t>(offset), offsetSet != JNI_FALSE);
    if (status == 0) {
        return answer(env, ghostlock::jni::CallCode::Ok, "", refusal_out);
    }
    const std::string named = status < 0
        ? (status == -1 ? "init:no-handle" : "init:stale-handle")
        : (std::string("init:") + ghostlock::contract::KernelAccess::status_name(
               static_cast<ghostlock::contract::KernelAccess::Status>(status)));
    return answer(env, ghostlock::jni::CallCode::Failed, named.c_str(), refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43499_Native43499_shutdown43499(
        JNIEnv *env, jclass, jlong, jlong accessId, jobjectArray refusal_out) {
    const int status = ghostlock::jni::device_shutdown_43499(static_cast<std::uint64_t>(accessId));
    if (status == 0) {
        return answer(env, ghostlock::jni::CallCode::Ok, "", refusal_out);
    }
    const std::string named = status < 0
        ? (status == -1 ? "shutdown:no-handle" : "shutdown:stale-handle")
        : (std::string("shutdown:") + ghostlock::contract::KernelAccess::status_name(
               static_cast<ghostlock::contract::KernelAccess::Status>(status)));
    return answer(env, ghostlock::jni::CallCode::Failed, named.c_str(), refusal_out);
}

}  // extern "C"
