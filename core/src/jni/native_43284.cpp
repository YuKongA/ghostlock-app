/** @file jni/native_43284.cpp
 *  libghostlock43284.so：43284 的 JNI 函数面（D2 装配 2026-10-15）。
 *
 *  参数是 D1 生成的 DTO，由同一张表的读取器逐字段复制；调用码与具名拒绝一次写回 refusalOut[0]；
 *  字节结果在会话锁内拷进本地缓冲，出锁后才造 Java 数组。
 */
#include <cstddef>
#include <cstdint>
#include <jni.h>
#include <vector>

#include "jni/api_43284.hpp"
#include "jni/api_43284_device.hpp"
#include "jni/api_43284_device.hpp"
#include "jni/attack_contract_reader.hpp"
#include "jni/wrapper_support.hpp"

namespace {
    using ghostlock::jni::Backend43284;
    using ghostlock::jni::CallCode;
    using ghostlock::jni::contract::ReadFailure;
    using ghostlock::jni::device_ops_43284;
    using ghostlock::jni::kBlockWidth;
    using ghostlock::jni::kImageLandingPayload;
    using ghostlock::jni::wrapper::answer;
    using ghostlock::jni::wrapper::has_room;
    using ghostlock::jni::wrapper::write_longs;

    using Registry = ghostlock::jni::wrapper::Registry<Backend43284>;

    Registry &registry() noexcept {
        static Registry table;
        return table;
    }

    [[nodiscard]] jbyteArray make_bytes(JNIEnv *env, const std::vector<std::uint8_t> &bytes) noexcept {
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

    /** 读取失败 ⇒ 让 backend 起一条具名拒绝（名字就是出错的字段）。 */
    [[nodiscard]] Registry::Outcome refuse_read(const jlong handle,
                                                const ReadFailure &failure) noexcept {
        return registry().with_answer(static_cast<std::uint64_t>(handle),
                                      [&failure](Backend43284 &backend) {
                                          return backend.reject_initialize(failure);
                                      });
    }

    /** 把镜像/载荷拷出来（在锁内调用的那个 lambda 里完成拷贝）。 */
    void copy_image(const std::span<const std::byte> image, std::vector<std::uint8_t> &out) noexcept {
        out.assign(reinterpret_cast<const std::uint8_t *>(image.data()),
                   reinterpret_cast<const std::uint8_t *>(image.data()) + image.size());
    }
}  // namespace

extern "C" {
/** 建会话：本构建自带的两张图与钩子计划在这里接上（其余缝属 J7.5 的设备工作）。 */
JNIEXPORT jlong JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_create(JNIEnv *, jclass) {
    registry().set_factory([]() noexcept { return Backend43284{device_ops_43284()}; });
    return static_cast<jlong>(registry().create());
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_initialize(
        JNIEnv *env, jclass, jlong handle, jobject params, jobjectArray refusal_out) {
    ghostlock::jni::Init43284Values values{};
    ReadFailure failure{};
    if (!ghostlock::jni::contract::readInit43284(env, params, &values, &failure)) {
        const auto outcome = refuse_read(handle, failure);
        return answer(env, outcome.code, outcome.refusal, refusal_out);
    }
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                                [&values](Backend43284 &backend) {
                                                    return backend.initialize(std::move(values));
                                                });
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_snapshot(
        JNIEnv *env, jclass, jlong handle, jlongArray out, jobjectArray refusal_out) {
    if (!has_room(env, out, 6U)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:out-array", refusal_out);
    }
    ghostlock::jni::Snapshot43284 snapshot{};
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                                [&snapshot](Backend43284 &backend) {
                                                    return backend.snapshot(snapshot);
                                                });
    if (outcome.code == CallCode::Ok) {
        const jlong values[6] = {
            static_cast<jlong>(snapshot.phase),  static_cast<jlong>(snapshot.flags),
            static_cast<jlong>(snapshot.width),  static_cast<jlong>(snapshot.ticket),
            static_cast<jlong>(snapshot.role),   static_cast<jlong>(snapshot.sequence),
        };
        static_cast<void>(write_longs(env, out, values, 6U));
    }
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_resolveTarget(
        JNIEnv *env, jclass, jlong handle, jobject request, jlongArray out, jobjectArray refusal_out) {
    if (!has_room(env, out, 1U)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:out-array", refusal_out);
    }
    ghostlock::jni::ResolveTarget43284Values values{};
    ReadFailure failure{};
    if (!ghostlock::jni::contract::readResolveTarget43284(env, request, &values, &failure)) {
        const auto outcome = refuse_read(handle, failure);
        return answer(env, outcome.code, outcome.refusal, refusal_out);
    }
    std::uint64_t ticket = 0U;
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                                [&values, &ticket](Backend43284 &backend) {
                                                    return backend.resolve_target(values, ticket);
                                                });
    if (outcome.code == CallCode::Ok) {
        const jlong written = static_cast<jlong>(ticket);
        static_cast<void>(write_longs(env, out, &written, 1U));
    }
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_verifyTarget(
        JNIEnv *env, jclass, jlong handle, jobject request, jobjectArray refusal_out) {
    ghostlock::jni::VerifyTarget43284Values values{};
    ReadFailure failure{};
    if (!ghostlock::jni::contract::readVerifyTarget43284(env, request, &values, &failure)) {
        const auto outcome = refuse_read(handle, failure);
        return answer(env, outcome.code, outcome.refusal, refusal_out);
    }
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                                [&values](Backend43284 &backend) {
                                                    return backend.verify_target(values);
                                                });
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_writeBlock(
        JNIEnv *env, jclass, jlong handle, jobject request, jobjectArray refusal_out) {
    ghostlock::jni::WriteBlock43284Values values{};
    ReadFailure failure{};
    if (!ghostlock::jni::contract::readWriteBlock43284(env, request, &values, &failure)) {
        const auto outcome = refuse_read(handle, failure);
        return answer(env, outcome.code, outcome.refusal, refusal_out);
    }
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                                [&values](Backend43284 &backend) {
                                                    return backend.write_block(values);
                                                });
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_readBlock(
        JNIEnv *env, jclass, jlong handle, jobject request, jbyteArray out, jobjectArray refusal_out) {
    if (out == nullptr || env->GetArrayLength(out) != static_cast<jsize>(kBlockWidth)) {
        return answer(env, CallCode::BadParameters, "bad-width", refusal_out);
    }
    ghostlock::jni::ReadBlock43284Values values{};
    ReadFailure failure{};
    if (!ghostlock::jni::contract::readReadBlock43284(env, request, &values, &failure)) {
        const auto outcome = refuse_read(handle, failure);
        return answer(env, outcome.code, outcome.refusal, refusal_out);
    }
    ghostlock::jni::ReadBlockOut43284 result{};
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                                [&values, &result](Backend43284 &backend) {
                                                    return backend.read_block(values, result);
                                                });
    if (outcome.code == CallCode::Ok && result.block.size() == kBlockWidth) {
        env->SetByteArrayRegion(out, 0, static_cast<jsize>(kBlockWidth),
                                reinterpret_cast<const jbyte *>(result.block.data()));
    }
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_moduleCheck(
        JNIEnv *env, jclass, jlong handle, jobject request, jobjectArray refusal_out) {
    ghostlock::jni::ModuleCheck43284Values values{};
    ReadFailure failure{};
    if (!ghostlock::jni::contract::readModuleCheck43284(env, request, &values, &failure)) {
        const auto outcome = refuse_read(handle, failure);
        return answer(env, outcome.code, outcome.refusal, refusal_out);
    }
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                                [&values](Backend43284 &backend) {
                                                    return backend.module_check(values);
                                                });
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_hookPlan(
        JNIEnv *env, jclass, jlong handle, jobject request, jlongArray out, jobjectArray refusal_out) {
    if (!has_room(env, out, 4U)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:out-array", refusal_out);
    }
    ghostlock::jni::HookPlanRequest43284Values values{};
    ReadFailure failure{};
    if (!ghostlock::jni::contract::readHookPlanRequest43284(env, request, &values, &failure)) {
        const auto outcome = refuse_read(handle, failure);
        return answer(env, outcome.code, outcome.refusal, refusal_out);
    }
    ghostlock::jni::HookPlanOut43284 plan{};
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                                [&values, &plan](Backend43284 &backend) {
                                                    return backend.hook_plan(values, plan);
                                                });
    if (outcome.code == CallCode::Ok) {
        const jlong words[4] = {
            static_cast<jlong>(plan.branch_word),    static_cast<jlong>(plan.displaced_word),
            static_cast<jlong>(plan.site_offset),    static_cast<jlong>(plan.landing_offset),
        };
        static_cast<void>(write_longs(env, out, words, 4U));
    }
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_trigger(
        JNIEnv *env, jclass, jlong handle, jobject request, jobjectArray refusal_out) {
    ghostlock::jni::Trigger43284Values values{};
    ReadFailure failure{};
    if (!ghostlock::jni::contract::readTrigger43284(env, request, &values, &failure)) {
        const auto outcome = refuse_read(handle, failure);
        return answer(env, outcome.code, outcome.refusal, refusal_out);
    }
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                                [&values](Backend43284 &backend) {
                                                    return backend.trigger(values);
                                                });
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_waitTerminal(
        JNIEnv *env, jclass, jlong handle, jobject request, jlongArray out, jobjectArray refusal_out) {
    if (!has_room(env, out, 1U)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:out-array", refusal_out);
    }
    ghostlock::jni::Wait43284Values values{};
    ReadFailure failure{};
    if (!ghostlock::jni::contract::readWait43284(env, request, &values, &failure)) {
        const auto outcome = refuse_read(handle, failure);
        return answer(env, outcome.code, outcome.refusal, refusal_out);
    }
    ghostlock::jni::TerminalState terminal = ghostlock::jni::TerminalState::InFlight;
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                                [&values, &terminal](Backend43284 &backend) {
                                                    return backend.wait_terminal(values, terminal);
                                                });
    if (outcome.code == CallCode::Ok || outcome.code == CallCode::InFlight ||
        outcome.code == CallCode::Failed) {
        const jlong written = static_cast<jlong>(terminal);
        static_cast<void>(write_longs(env, out, &written, 1U));
    }
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

JNIEXPORT jbyteArray JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_embeddedImage(
        JNIEnv *env, jclass, jlong handle, jint role, jobjectArray refusal_out) {
    std::vector<std::uint8_t> copied{};
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                                [role, &copied](Backend43284 &backend) {
                                                    std::span<const std::byte> image{};
                                                    const CallCode code = backend.embedded_image(
                                                        static_cast<std::int32_t>(role), image);
                                                    if (code == CallCode::Ok) {
                                                        copy_image(image, copied);
                                                    }
                                                    return code;
                                                });
    static_cast<void>(answer(env, outcome.code, outcome.refusal, refusal_out));
    return outcome.code == CallCode::Ok ? make_bytes(env, copied) : nullptr;
}

JNIEXPORT jbyteArray JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_hookPayload(
        JNIEnv *env, jclass, jlong handle, jobjectArray refusal_out) {
    std::vector<std::uint8_t> copied{};
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                                [&copied](Backend43284 &backend) {
                                                    std::span<const std::byte> image{};
                                                    const CallCode code = backend.embedded_image(
                                                        kImageLandingPayload, image);
                                                    if (code == CallCode::Ok) {
                                                        copy_image(image, copied);
                                                    }
                                                    return code;
                                                });
    static_cast<void>(answer(env, outcome.code, outcome.refusal, refusal_out));
    return outcome.code == CallCode::Ok ? make_bytes(env, copied) : nullptr;
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_shutdown(
        JNIEnv *env, jclass, jlong handle, jobjectArray refusal_out) {
    const auto outcome = registry().with_answer(static_cast<std::uint64_t>(handle),
                                                [](Backend43284 &backend) {
                                                    return backend.shutdown();
                                                });
    return answer(env, outcome.code, outcome.refusal, refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_destroy(
        JNIEnv *env, jclass, jlong handle, jobjectArray refusal_out) {
    const bool retired = registry().retire(static_cast<std::uint64_t>(handle));
    return answer(env, retired ? CallCode::Ok : CallCode::StaleHandle,
                  retired ? "" : "stale-handle", refusal_out);
}

JNIEXPORT jobjectArray JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_snapshotFields(
        JNIEnv *env, jclass) {
    static const char *const kNames[] = {"phase", "flags", "width", "ticket", "role", "sequence"};
    return ghostlock::jni::wrapper::make_string_array(env, kNames, 6U);
}
JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_closeTarget(
        JNIEnv *env, jclass, jlong, jlong targetId, jobjectArray refusal_out) {
    const bool closed = ghostlock::jni::device_close_target(static_cast<std::uint64_t>(targetId));
    return answer(env, closed ? CallCode::Ok : CallCode::StaleTarget,
                  closed ? "" : "stale-target", refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_connectEsp(
        JNIEnv *env, jclass, jlong handle, jobject params, jobjectArray refusal_out) {
    ghostlock::jni::Init43284Values values{};
    ghostlock::jni::contract::ReadFailure failure{};
    if (!ghostlock::jni::contract::readInit43284(env, params, &values, &failure)) {
        const auto outcome = refuse_read(handle, failure);
        return answer(env, outcome.code, outcome.refusal, refusal_out);
    }
    const int fd = ghostlock::jni::device_connect_esp(values);
    if (fd < 0) {
        const char *const why = fd == -1 ? "esp-no-material"
                                : fd == -2 ? "esp-socket"
                                : fd == -3 ? "esp-bind"
                                           : "esp-connect";
        return answer(env, CallCode::Failed, why, refusal_out);
    }
    return answer(env, CallCode::Ok, "", refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_closeSocket(
        JNIEnv *env, jclass, jlong, jint fd, jobjectArray refusal_out) {
    const bool closed = ghostlock::jni::device_close_socket(static_cast<int>(fd));
    return answer(env, closed ? CallCode::Ok : CallCode::BadParameters,
                  closed ? "" : "bad-parameters:fd", refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_espSocketId(
        JNIEnv *env, jclass, jlong handle, jobject params, jlongArray out, jobjectArray refusal_out) {
    if (!has_room(env, out, 1U)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:out-array", refusal_out);
    }
    ghostlock::jni::Init43284Values values{};
    ghostlock::jni::contract::ReadFailure failure{};
    if (!ghostlock::jni::contract::readInit43284(env, params, &values, &failure)) {
        const auto outcome = refuse_read(handle, failure);
        return answer(env, outcome.code, outcome.refusal, refusal_out);
    }
    int why = 0;
    const std::uint64_t socket_id = ghostlock::jni::device_esp_socket_id(values, why);
    if (socket_id == 0U) {
        const char *const named = why == -1 ? "esp-no-material"
                                 : why == -2 ? "esp-socket"
                                 : why == -3 ? "esp-bind"
                                 : why == -4 ? "esp-connect"
                                 : why == -9 ? "esp-table-full"
                                            : "esp-refused";
        return answer(env, CallCode::Failed, named, refusal_out);
    }
    const jlong written = static_cast<jlong>(socket_id);
    static_cast<void>(write_longs(env, out, &written, 1U));
    return answer(env, CallCode::Ok, "", refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_closeSocketId(
        JNIEnv *env, jclass, jlong, jlong socketId, jobjectArray refusal_out) {
    const bool done = ghostlock::jni::device_close_socket_id(static_cast<std::uint64_t>(socketId));
    return answer(env, done ? CallCode::Ok : CallCode::StaleTarget,
                  done ? "" : "stale-target", refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_destroySurface(
        JNIEnv *env, jclass, jlong, jlong surfaceId, jobjectArray refusal_out) {
    const bool done = ghostlock::jni::device_destroy_surface(static_cast<std::uint64_t>(surfaceId));
    return answer(env, done ? CallCode::Ok : CallCode::StaleTarget,
                  done ? "" : "stale-target", refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_destroyLanding(
        JNIEnv *env, jclass, jlong, jlong landingId, jobjectArray refusal_out) {
    const bool done = ghostlock::jni::device_destroy_landing(static_cast<std::uint64_t>(landingId));
    return answer(env, done ? CallCode::Ok : CallCode::StaleTarget,
                  done ? "" : "stale-target", refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_makeSurface(
        JNIEnv *env, jclass, jlong, jlong targetId, jlong socketId, jlongArray out, jobjectArray refusal_out) {
    if (!has_room(env, out, 1U)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:out-array", refusal_out);
    }
    const std::uint64_t made = ghostlock::jni::device_make_surface(static_cast<std::uint64_t>(targetId), static_cast<std::uint64_t>(socketId));
    if (made == 0U) {
        return answer(env, CallCode::Failed, "surface-refused", refusal_out);
    }
    const jlong written = static_cast<jlong>(made);
    static_cast<void>(write_longs(env, out, &written, 1U));
    return answer(env, CallCode::Ok, "", refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_makeLanding(
        JNIEnv *env, jclass, jlong, jlong surfaceId, jlongArray out, jobjectArray refusal_out) {
    if (!has_room(env, out, 1U)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:out-array", refusal_out);
    }
    const std::uint64_t made = ghostlock::jni::device_make_landing(static_cast<std::uint64_t>(surfaceId));
    if (made == 0U) {
        return answer(env, CallCode::Failed, "landing-refused", refusal_out);
    }
    const jlong written = static_cast<jlong>(made);
    static_cast<void>(write_longs(env, out, &written, 1U));
    return answer(env, CallCode::Ok, "", refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_bringUp(
        JNIEnv *env, jclass, jlong handle, jlong landingId, jobject params, jobjectArray refusal_out) {
    ghostlock::jni::Init43284Values values{};
    ghostlock::jni::contract::ReadFailure failure{};
    if (!ghostlock::jni::contract::readInit43284(env, params, &values, &failure)) {
        const auto outcome = refuse_read(handle, failure);
        return answer(env, outcome.code, outcome.refusal, refusal_out);
    }
    const bool up = ghostlock::jni::device_bring_up(static_cast<std::uint64_t>(landingId), values);
    return answer(env, up ? CallCode::Ok : CallCode::Failed, up ? "" : "bring-up-refused", refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_nextSequence(
        JNIEnv *env, jclass, jlong, jlong landingId, jlongArray out, jobjectArray refusal_out) {
    if (!has_room(env, out, 1U)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:out-array", refusal_out);
    }
    const std::uint64_t next = ghostlock::jni::device_next_sequence(static_cast<std::uint64_t>(landingId));
    const jlong written = static_cast<jlong>(next);
    static_cast<void>(write_longs(env, out, &written, 1U));
    return answer(env, CallCode::Ok, "", refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_adoptSequence(
        JNIEnv *env, jclass, jlong, jlong landingId, jlong next, jobjectArray refusal_out) {
    const bool adopted = ghostlock::jni::device_adopt_sequence(
        static_cast<std::uint64_t>(landingId), static_cast<std::uint64_t>(next));
    return answer(env, adopted ? CallCode::Ok : CallCode::StaleTarget,
                  adopted ? "" : "stale-target", refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_landBlock(
        JNIEnv *env, jclass, jlong, jlong landingId, jlong offset, jbyteArray block,
        jobjectArray refusal_out) {
    if (block == nullptr || env->GetArrayLength(block) != 16) {
        return answer(env, CallCode::BadParameters, "bad-width", refusal_out);
    }
    std::uint8_t bytes[16] = {};
    env->GetByteArrayRegion(block, 0, 16, reinterpret_cast<jbyte *>(bytes));
    const bool written = ghostlock::jni::device_land_block(
        static_cast<std::uint64_t>(landingId), static_cast<std::uint64_t>(offset), bytes);
    return answer(env, written ? CallCode::Ok : CallCode::Failed,
                  written ? "" : "block-refused", refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_readLandingBlock(
        JNIEnv *env, jclass, jlong, jlong landingId, jlong offset, jbyteArray out,
        jobjectArray refusal_out) {
    if (out == nullptr || env->GetArrayLength(out) != 16) {
        return answer(env, CallCode::BadParameters, "bad-width", refusal_out);
    }
    std::uint8_t bytes[16] = {};
    const bool read = ghostlock::jni::device_read_landing_block(
        static_cast<std::uint64_t>(landingId), static_cast<std::uint64_t>(offset), bytes);
    if (read) {
        env->SetByteArrayRegion(out, 0, 16, reinterpret_cast<const jbyte *>(bytes));
    }
    return answer(env, read ? CallCode::Ok : CallCode::Failed,
                  read ? "" : "block-read-refused", refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_makeAccess(
        JNIEnv *env, jclass, jlong, jlong landingId, jlongArray out, jobjectArray refusal_out) {
    if (!has_room(env, out, 1U)) {
        return answer(env, CallCode::BadParameters, "bad-parameters:out-array", refusal_out);
    }
    const std::uint64_t made = ghostlock::jni::device_make_access(static_cast<std::uint64_t>(landingId));
    if (made == 0U) {
        return answer(env, CallCode::Failed, "access-refused", refusal_out);
    }
    const jlong written = static_cast<jlong>(made);
    static_cast<void>(write_longs(env, out, &written, 1U));
    return answer(env, CallCode::Ok, "", refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_destroyAccess(
        JNIEnv *env, jclass, jlong, jlong accessId, jobjectArray refusal_out) {
    const bool done = ghostlock::jni::device_destroy_access(static_cast<std::uint64_t>(accessId));
    return answer(env, done ? CallCode::Ok : CallCode::StaleTarget,
                  done ? "" : "stale-target", refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_accessInitialize(
        JNIEnv *env, jclass, jlong handle, jlong accessId, jobject params, jobjectArray refusal_out) {
    ghostlock::jni::contract::AccessParams43284Values values{};
    ghostlock::jni::contract::ReadFailure failure{};
    if (!ghostlock::jni::contract::readAccessParams43284(env, params, &values, &failure)) {
        const auto outcome = refuse_read(handle, failure);
        return answer(env, outcome.code, outcome.refusal, refusal_out);
    }
    const bool up = ghostlock::jni::device_access_initialize(
        static_cast<std::uint64_t>(accessId), values);
    return answer(env, up ? CallCode::Ok : CallCode::Failed,
                  up ? "" : "access-initialize-refused", refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_writeImage(
        JNIEnv *env, jclass, jlong handle, jobject params, jobject image, jobjectArray refusal_out) {
    ghostlock::jni::contract::AccessParams43284Values params_values{};
    ghostlock::jni::contract::ReadFailure params_failure{};
    if (!ghostlock::jni::contract::readAccessParams43284(env, params, &params_values, &params_failure)) {
        const auto outcome = refuse_read(handle, params_failure);
        return answer(env, outcome.code, outcome.refusal, refusal_out);
    }
    ghostlock::jni::contract::WriteImage43284Values values{};
    ghostlock::jni::contract::ReadFailure failure{};
    if (!ghostlock::jni::contract::readWriteImage43284(env, image, &values, &failure)) {
        const auto outcome = refuse_read(handle, failure);
        return answer(env, outcome.code, outcome.refusal, refusal_out);
    }
    const bool wrote = ghostlock::jni::device_write_image(values, params_values);
    return answer(env, wrote ? CallCode::Ok : CallCode::Failed,
                  wrote ? "" : "image-write-refused", refusal_out);
}

JNIEXPORT jint JNICALL Java_com_ghostlock_app_jni_cve43284_Native43284_writeHook(
        JNIEnv *env, jclass, jlong handle, jobject params, jobject hook, jobjectArray refusal_out) {
    ghostlock::jni::contract::AccessParams43284Values params_values{};
    ghostlock::jni::contract::ReadFailure params_failure{};
    if (!ghostlock::jni::contract::readAccessParams43284(env, params, &params_values, &params_failure)) {
        const auto outcome = refuse_read(handle, params_failure);
        return answer(env, outcome.code, outcome.refusal, refusal_out);
    }
    ghostlock::jni::contract::WriteHook43284Values values{};
    ghostlock::jni::contract::ReadFailure failure{};
    if (!ghostlock::jni::contract::readWriteHook43284(env, hook, &values, &failure)) {
        const auto outcome = refuse_read(handle, failure);
        return answer(env, outcome.code, outcome.refusal, refusal_out);
    }
    const bool wrote = ghostlock::jni::device_write_hook(values, params_values);
    return answer(env, wrote ? CallCode::Ok : CallCode::Failed,
                  wrote ? "" : "hook-write-refused", refusal_out);
}

}  // extern "C"
