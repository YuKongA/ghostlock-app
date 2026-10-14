/** @file jni/session_api.cpp
 *  The four JNI entries: create, invoke, shutdown, destroy -- and nothing else.
 *
 *  This file is the boundary's thinnest layer on purpose: it copies the frame out of Java, hands it
 *  to the session object, and copies the answer back. Every rule worth testing lives in
 *  session_protocol.cpp / session_object.cpp, which are JNI-free and run on the host; what is left
 *  here is the glue that cannot be tested without an Android VM:
 *
 *   - the registry (see below), which is the only mutable state in this file;
 *   - the handle's presentation as a jlong (encode/decode live in session_handles.hpp);
 *   - zeroing the copy of a request frame, because a frame may carry the credentials.
 *
 *  Registry: the JNI side has no caller-provided context, so the handle table and the sessions
 *  live in one process-wide object. AGENTS allows one mutable global per layer; this is the JNI
 *  layer's, and it is not reachable from any other layer.
 *
 *  Refusal convention: an entry that cannot do its work answers with the FrameStatus as a negative
 *  jlong/jint (the codes are the same table the frame carries), and the Kotlin side maps the number
 *  back to an enum. A positive jlong from create is a live handle.
 */
#include <jni.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

#include "contract/kernel_access.hpp"
#include "jni/session_handles.hpp"
#include "jni/session_object.hpp"
#include "jni/session_protocol.hpp"

namespace {
    using ghostlock::jni::FrameOp;
    using ghostlock::jni::FrameReader;
    using ghostlock::jni::FrameStatus;
    using ghostlock::jni::FrameWriter;
    using ghostlock::jni::HandleOutcome;
    using ghostlock::jni::kFrameMaxBytes;
    using ghostlock::jni::kMaxSessions;
    using ghostlock::jni::Session;

    /** Everything the boundary holds. One instance, made on first use. */
    struct Registry final {
        std::mutex table_mutex{};
        ghostlock::jni::HandleTable table{};
        Session *sessions[kMaxSessions] = {};
        /** One lock per session: two threads may work on two sessions without meeting. */
        std::mutex session_locks[kMaxSessions]{};
    };

    [[nodiscard]] Registry &registry() noexcept {
        static Registry instance;
        return instance;
    }

    /** The real backend factory: the contract's, which knows which backends this build has. */
    [[nodiscard]] std::unique_ptr<ghostlock::contract::KernelAccess> create_backend(
        std::uint64_t backend_id, void *) noexcept {
        return ghostlock::contract::KernelAccess::create(
            static_cast<ghostlock::contract::BackendId>(backend_id));
    }

    /** Copy a Java byte[] into a vector, refusing anything larger than one frame. */
    [[nodiscard]] bool copy_frame(JNIEnv *env, jbyteArray array, std::vector<std::byte> &out) {
        out.clear();
        if (array == nullptr) {
            return false;
        }
        const jsize length = env->GetArrayLength(array);
        if (length < 0 || static_cast<std::size_t>(length) > kFrameMaxBytes) {
            return false;
        }
        out.resize(static_cast<std::size_t>(length));
        if (length > 0) {
            env->GetByteArrayRegion(array, 0, length, reinterpret_cast<jbyte *>(out.data()));
        }
        return true;
    }

    /** Put a reply frame into a new Java byte[], or null when there is nothing to answer. */
    [[nodiscard]] jbyteArray to_array(JNIEnv *env, std::span<const std::byte> bytes) {
        auto *const array = env->NewByteArray(static_cast<jsize>(bytes.size()));
        if (array == nullptr) {
            return nullptr;
        }
        if (!bytes.empty()) {
            env->SetByteArrayRegion(array, 0, static_cast<jsize>(bytes.size()),
                                    reinterpret_cast<const jbyte *>(bytes.data()));
        }
        return array;
    }

    /** A minimal answer frame carrying only a status and a name. */
    [[nodiscard]] jbyteArray refusal_frame(JNIEnv *env, FrameOp op, FrameStatus status,
                                           const char *name) {
        std::byte buffer[128]{};
        FrameWriter writer{std::span<std::byte>(buffer)};
        if (!writer.begin(op)) {
            return to_array(env, std::span<const std::byte>());
        }
        if (name != nullptr) {
            const std::string_view text(name);
            static_cast<void>(writer.put_bytes(
                ghostlock::jni::FrameKey::Text,
                std::span<const std::byte>(reinterpret_cast<const std::byte *>(text.data()),
                                           text.size())));
        }
        static_cast<void>(writer.finish(status));
        return to_array(env, writer.view());
    }
}  // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_jni_Native_nativeSessionCreate(JNIEnv *env, jclass, jbyteArray createFrame) {
    std::vector<std::byte> frame;
    if (!copy_frame(env, createFrame, frame)) {
        return static_cast<jlong>(FrameStatus::TooBig);
    }
    FrameReader reader;
    FrameOp op = FrameOp::Snapshot;
    const FrameStatus opened = reader.open(std::span<const std::byte>(frame), op);
    if (opened != FrameStatus::Ok) {
        ghostlock::jni::zero_bytes(std::span<std::byte>(frame));
        return static_cast<jlong>(opened);
    }
    if (op != FrameOp::Initialize) {
        /* A create frame is an initialize request: the backend id and its parameters ride in it. */
        ghostlock::jni::zero_bytes(std::span<std::byte>(frame));
        return static_cast<jlong>(FrameStatus::Malformed);
    }
    std::uint64_t backend_id = 0U;
    if (!reader.find_u64(ghostlock::jni::FrameKey::Backend, backend_id)) {
        ghostlock::jni::zero_bytes(std::span<std::byte>(frame));
        return static_cast<jlong>(FrameStatus::KeyMissing);
    }
    ghostlock::jni::zero_bytes(std::span<std::byte>(frame));

    Registry &state = registry();
    std::lock_guard<std::mutex> guard(state.table_mutex);
    std::uint32_t index = 0U;
    std::uint32_t generation = 0U;
    const HandleOutcome minted = state.table.mint(index, generation);
    if (minted != HandleOutcome::Ok) {
        return static_cast<jlong>(FrameStatus::Busy);
    }
    auto session = std::make_unique<Session>();
    const ghostlock::jni::BackendSeam seam{&create_backend, nullptr};
    if (!session->open(backend_id, seam, generation)) {
        /* The session stays alive: it answers Snapshot with Failed, which is what a caller needs to
         *  see ("this build has no such backend") instead of a handle that pretends to work. */
    }
    state.sessions[index] = session.release();
    return static_cast<jlong>(ghostlock::jni::encode_handle(index, generation));
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_ghostlock_app_jni_Native_nativeSessionInvoke(JNIEnv *env, jclass, jlong handle, jint op_raw,
                                                      jbyteArray requestFrame) {
    std::uint32_t index = 0U;
    std::uint32_t generation = 0U;
    if (!ghostlock::jni::decode_handle(static_cast<std::uint64_t>(handle), index, generation)) {
        return refusal_frame(env, FrameOp::Snapshot, FrameStatus::NotReady, "unknown-handle");
    }
    Registry &state = registry();
    {
        std::lock_guard<std::mutex> guard(state.table_mutex);
        if (state.table.resolve(index, generation) != HandleOutcome::Ok) {
            return refusal_frame(env, FrameOp::Snapshot, FrameStatus::StaleTarget, "stale-handle");
        }
    }
    std::vector<std::byte> frame;
    if (!copy_frame(env, requestFrame, frame)) {
        return refusal_frame(env, FrameOp::Snapshot, FrameStatus::TooBig, "frame-too-big");
    }
    FrameReader reader;
    FrameOp op = FrameOp::Snapshot;
    const FrameStatus opened = reader.open(std::span<const std::byte>(frame), op);
    if (opened != FrameStatus::Ok) {
        ghostlock::jni::zero_bytes(std::span<std::byte>(frame));
        return refusal_frame(env, FrameOp::Snapshot, opened, nullptr);
    }
    const FrameOp asked = ghostlock::jni::frame_op_of(static_cast<std::uint16_t>(op_raw));
    if (asked == FrameOp::Count || asked != op) {
        /* The argument and the frame must agree: two authorities for one fact is the mistake the
         *  wire exists to prevent. */
        ghostlock::jni::zero_bytes(std::span<std::byte>(frame));
        return refusal_frame(env, op, FrameStatus::Malformed, "op-argument-mismatch");
    }

    std::vector<std::byte> answer(kFrameMaxBytes);
    FrameWriter writer{std::span<std::byte>(answer)};
    FrameStatus status = FrameStatus::Failed;
    {
        std::lock_guard<std::mutex> session_guard(state.session_locks[index]);
        Session *const session = state.sessions[index];
        if (session == nullptr) {
            ghostlock::jni::zero_bytes(std::span<std::byte>(frame));
            return refusal_frame(env, op, FrameStatus::Closed, "no-session");
        }
        if (!writer.begin(op)) {
            ghostlock::jni::zero_bytes(std::span<std::byte>(frame));
            return refusal_frame(env, op, FrameStatus::TooBig, nullptr);
        }
        status = session->invoke(op, reader, writer);
        static_cast<void>(writer.finish(status));
    }
    /* The request may have carried credentials; the copy does not outlive this call. */
    ghostlock::jni::zero_bytes(std::span<std::byte>(frame));
    return to_array(env, writer.view());
}

extern "C" JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_Native_nativeSessionShutdown(JNIEnv *env, jclass, jlong handle) {
    static_cast<void>(env);
    std::uint32_t index = 0U;
    std::uint32_t generation = 0U;
    if (!ghostlock::jni::decode_handle(static_cast<std::uint64_t>(handle), index, generation)) {
        return static_cast<jint>(FrameStatus::NotReady);
    }
    Registry &state = registry();
    std::lock_guard<std::mutex> guard(state.table_mutex);
    if (state.table.resolve(index, generation) != HandleOutcome::Ok) {
        return static_cast<jint>(FrameStatus::StaleTarget);
    }
    std::lock_guard<std::mutex> session_guard(state.session_locks[index]);
    Session *const session = state.sessions[index];
    if (session == nullptr) {
        return static_cast<jint>(FrameStatus::Closed);
    }
    return static_cast<jint>(session->shutdown());
}

extern "C" JNIEXPORT void JNICALL
Java_com_ghostlock_app_jni_Native_nativeSessionDestroy(JNIEnv *env, jclass, jlong handle) {
    static_cast<void>(env);
    std::uint32_t index = 0U;
    std::uint32_t generation = 0U;
    if (!ghostlock::jni::decode_handle(static_cast<std::uint64_t>(handle), index, generation)) {
        return;
    }
    Registry &state = registry();
    std::lock_guard<std::mutex> guard(state.table_mutex);
    if (state.table.resolve(index, generation) != HandleOutcome::Ok) {
        /* Destroying a handle twice, or one from a previous generation, is not an error: the caller
         *  asked for the session to be gone, and it is. */
        return;
    }
    {
        std::lock_guard<std::mutex> session_guard(state.session_locks[index]);
        delete state.sessions[index];
        state.sessions[index] = nullptr;
    }
    state.table.retire(index);
}
