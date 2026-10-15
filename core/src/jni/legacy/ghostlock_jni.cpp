/** @file jni/ghostlock_jni.cpp
 *  The boundary: three entries, and the session object they act on.
 *
 *  Kotlin calls nativeSessionCreate once with the frame (see session_frame.hpp), runs writes
 *  with nativeRunOneWrite, and calls nativeSessionDestroy when finished.
 *
 *  PROVISIONAL PACKING. The frame layout session_frame.hpp describes is a proposal; it has not
 *  been accepted. It is implemented here so the boundary is real and testable, and it is kept
 *  in one place -- unpack_frame below -- so that changing to a different carrier (several
 *  longs, or several entries) touches this function and nothing else.
 */
#include <jni.h>

#include <cstdint>
#include <cstring>
#include <new>

#include "jni/pselect_route.hpp"
#include "jni/race_threads.hpp"
#include "jni/route_landings.hpp"
#include "jni/session_frame.hpp"
/* 旧码（原文保留 ✓）：
 *     #include "jni/write_one.hpp"
 *  TODO(jni-two-backend): 2026-10-14 —— write_one.hpp 在同一命名空间里**重复定义**了一份
 *  形状不同的 SprayProducts（另一份在 route_common.hpp）⇒ 两处同时包含即 redefinition ✗。
 *  本入口不调用 do_one_write，故这里去掉该 include 与相应成员；write_one.{hpp,cpp} 列入
 *  Makefile 的 legacy 排除表。完成 = J5 删/改旧入口 ⇒ 删本注释块。 */
#include "support/log.hpp"

namespace {
    /** The refusal codes the boundary reports. */
    constexpr jint kOk = 0;
    constexpr jint kBadHandle = -1;
    constexpr jint kNoMemory = -2;
    constexpr jint kBadFrame = -3;
    constexpr jint kNoDriver = -4;

    /** Everything one run owns. Filled by nativeSessionCreate, used by nativeRunOneWrite. */
    struct Session final {
        ghostlock::jni::race::RaceContext race{};
        ghostlock::jni::WriteOneParams params{};
        ghostlock::jni::DirectMapBounds bounds{};
        /* products 只被 write_one.cpp 消费；本入口不调用它，且其类型来自 route_common.hpp。 */
        ghostlock::jni::SprayProducts products{};
        ghostlock::jni::SprayOffsets offsets{};
        ghostlock::jni::PselectTuning tuning{};
        ghostlock::jni::RouteAddrs addrs{};
        ghostlock::jni::WriteSeams ops{};
        ghostlock::jni::RouteSeams route_landings{};
        ghostlock::jni::race::RaceWords words{};
        std::uint32_t waiter_shift = 0U;
        bool compact_waiter = false;
    };

    Session *from_handle(const jlong handle) noexcept {
        return reinterpret_cast<Session *>(static_cast<std::uintptr_t>(handle));
    }
}  // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_jni_Native_nativeSessionCreate(JNIEnv *env, jclass, jbyteArray frame) {
    if (frame == nullptr) {
        return static_cast<jlong>(kBadFrame);
    }
    auto *const session = new (std::nothrow) Session{};
    if (session == nullptr) {
        return static_cast<jlong>(kNoMemory);
    }
    /* Copy the frame out, then read it. The copy is because a JNI buffer must not be held
     *  past the call that produced it. */
    const jsize length = env->GetArrayLength(frame);
    if (length < static_cast<jsize>(ghostlock::jni::SessionFrame::kHeaderSize)) {
        delete session;
        return static_cast<jlong>(kBadFrame);
    }
    std::uint8_t header[ghostlock::jni::SessionFrame::kHeaderSize]{};
    env->GetByteArrayRegion(frame, 0, static_cast<jsize>(ghostlock::jni::SessionFrame::kHeaderSize),
                            reinterpret_cast<jbyte *>(header));
    std::uint32_t magic = 0U;
    std::memcpy(&magic, header, sizeof(magic));
    if (magic != ghostlock::jni::SessionFrame::kMagic) {
        delete session;
        return static_cast<jlong>(kBadFrame);
    }
    /* The body arrives later, with the write. Only the two switches are read here, because
     *  they decide which route family the run uses. */
    session->compact_waiter = (header[12] & 0x01U) != 0U;
    session->race.route = header[8] == 0U ? ghostlock::jni::race::RouteChoice::Pselect
                                          : ghostlock::jni::race::RouteChoice::Tcp;
    session->race.offsets.compact_waiter = session->compact_waiter;
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(session));
}

extern "C" JNIEXPORT void JNICALL
Java_com_ghostlock_app_jni_Native_nativeSessionDestroy(JNIEnv *, jclass, jlong handle) {
    delete from_handle(handle);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_Native_nativeRunOneWrite(JNIEnv *_env, jclass, jlong handle,
                                                   jbyteArray frame) {
    Session *const session = from_handle(handle);
    if (session == nullptr || frame == nullptr) {
        return kBadHandle;
    }
    /* The carry of the parameters is provisional (see the header). What matters here is that
     *  the call goes to the driver this experiment moved, not that the bytes were laid out in
     *  the way the proposal describes. */
    const bool routed = ghostlock::jni::run_main_route_threads(session->race);
    if (!routed) {
        ghostlock::support::log("warning", "one write did not reach a verified write");
        return kNoDriver;
    }
    return kOk;
}
