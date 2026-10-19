/** Test-only probe: execute the DFRoot payload in a forked child with a fake site. */
#include <jni.h>

#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <new>

namespace {
    std::uint32_t branch(std::uint64_t from, std::uint64_t to) noexcept {
        const auto delta = static_cast<std::int64_t>(to - from);
        return 0x14000000U | (static_cast<std::uint32_t>(delta >> 2) & 0x03FFFFFFU);
    }

    int run_child(const std::uint8_t *payload, std::size_t size) noexcept {
        auto *base = static_cast<std::uint8_t *>(
            ::mmap(nullptr, 0x3000, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (base == MAP_FAILED) {
            return 90;
        }
        std::memcpy(base, payload, size);
        auto *site = base + 0x800;
        const auto *entry = base + 0x90;
        *reinterpret_cast<std::uint32_t *>(site) = branch(
            reinterpret_cast<std::uintptr_t>(site),
            reinterpret_cast<std::uintptr_t>(entry));
        *reinterpret_cast<std::uint32_t *>(site + 4) = 0xD2800540U;  /* mov x0,#42 */
        *reinterpret_cast<std::uint32_t *>(site + 8) = 0xD2800BA8U;  /* mov x8,#93 */
        *reinterpret_cast<std::uint32_t *>(site + 12) = 0xD4000001U; /* svc */
        /* The payload's return path executes first-inst copy, then jumpback. */
        *reinterpret_cast<std::uint32_t *>(base + 0x1C8) = 0xA9BE7BFDU; /* original sentry stp */
        *reinterpret_cast<std::uint32_t *>(base + 0x1CC) = branch(
            reinterpret_cast<std::uintptr_t>(base + 0x1CC),
            reinterpret_cast<std::uintptr_t>(site + 4));
        __builtin___clear_cache(reinterpret_cast<char *>(base),
                                reinterpret_cast<char *>(base + 0x3000));
        reinterpret_cast<void (*)()>(site)();
        return 99;
    }
}  // namespace

extern "C" JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_cve43284_NativePayloadProbe_run(
        JNIEnv *env, jclass, jbyteArray payload) {
    if (env == nullptr || payload == nullptr) {
        return -1;
    }
    const jsize size = env->GetArrayLength(payload);
    if (size <= 0 || size > 0x800) {
        return -2;
    }
    auto *bytes = new (std::nothrow) std::uint8_t[static_cast<std::size_t>(size)];
    if (bytes == nullptr) {
        return -3;
    }
    env->GetByteArrayRegion(payload, 0, size, reinterpret_cast<jbyte *>(bytes));
    const pid_t child = ::fork();
    if (child < 0) {
        delete[] bytes;
        return -4;
    }
    if (child == 0) {
        _exit(run_child(bytes, static_cast<std::size_t>(size)));
    }
    int status = 0;
    ::waitpid(child, &status, 0);
    delete[] bytes;
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return -5;
}
