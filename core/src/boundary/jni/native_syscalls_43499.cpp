/** @file boundary/jni/native_syscalls_43499.cpp
 *  43499 kt-step 的 JNI syscall 薄适配器：只把 Java 参数搬到平台函数，返回 -errno 或值。
 *  无状态、无路由、无页/会话逻辑、无缓存。
 */
#include <jni.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>

#include "platform/syscalls_43499.hpp"

namespace {
    [[nodiscard]] jlong answer(const long value) noexcept { return static_cast<jlong>(value); }

    [[nodiscard]] bool has_bytes(JNIEnv *env, jbyteArray array) noexcept {
        return array != nullptr && env->GetArrayLength(array) >= 0;
    }
}  // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeSyscalls43499_futex(
    JNIEnv *, jclass, jlong uaddr, jint op, jint value, jlong timeout_ns, jlong uaddr2, jint value3) {
    return answer(ghostlock::platform::syscalls43499::futex(
        static_cast<std::uintptr_t>(uaddr), static_cast<int>(op), static_cast<int>(value),
        static_cast<std::int64_t>(timeout_ns), static_cast<std::uintptr_t>(uaddr2),
        static_cast<int>(value3)));
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeSyscalls43499_mmap(
    JNIEnv *, jclass, jlong length, jint prot, jint flags, jint fd, jlong offset) {
    return answer(ghostlock::platform::syscalls43499::mmap(
        static_cast<std::uintptr_t>(length), static_cast<int>(prot), static_cast<int>(flags),
        static_cast<int>(fd), static_cast<std::int64_t>(offset)));
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeSyscalls43499_munmap(
    JNIEnv *, jclass, jlong address, jlong length) {
    return answer(ghostlock::platform::syscalls43499::munmap(
        static_cast<std::uintptr_t>(address), static_cast<std::uintptr_t>(length)));
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeSyscalls43499_memfdCreate(
    JNIEnv *env, jclass, jstring name, jint flags) {
    if (name == nullptr) return answer(-EINVAL);
    const char *const utf = env->GetStringUTFChars(name, nullptr);
    if (utf == nullptr) return answer(-ENOMEM);
    const long result = ghostlock::platform::syscalls43499::memfd_create(
        utf, static_cast<unsigned int>(flags));
    env->ReleaseStringUTFChars(name, utf);
    return answer(result);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeSyscalls43499_socket(
    JNIEnv *, jclass, jint domain, jint type, jint protocol) {
    return answer(ghostlock::platform::syscalls43499::socket(
        static_cast<int>(domain), static_cast<int>(type), static_cast<int>(protocol)));
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeSyscalls43499_setsockopt(
    JNIEnv *env, jclass, jint fd, jint level, jint option, jbyteArray value) {
    if (!has_bytes(env, value)) return answer(-EINVAL);
    const jsize length = env->GetArrayLength(value);
    jbyte *const bytes = env->GetByteArrayElements(value, nullptr);
    if (bytes == nullptr) return answer(-ENOMEM);
    const long result = ghostlock::platform::syscalls43499::setsockopt(
        static_cast<int>(fd), static_cast<int>(level), static_cast<int>(option), bytes,
        static_cast<std::size_t>(length));
    env->ReleaseByteArrayElements(value, bytes, JNI_ABORT);
    return answer(result);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeSyscalls43499_send(
    JNIEnv *env, jclass, jint fd, jbyteArray buffer, jint flags) {
    if (!has_bytes(env, buffer)) return answer(-EINVAL);
    const jsize length = env->GetArrayLength(buffer);
    jbyte *const bytes = env->GetByteArrayElements(buffer, nullptr);
    if (bytes == nullptr) return answer(-ENOMEM);
    const long result = ghostlock::platform::syscalls43499::send(
        static_cast<int>(fd), bytes, static_cast<std::size_t>(length), static_cast<int>(flags));
    env->ReleaseByteArrayElements(buffer, bytes, JNI_ABORT);
    return answer(result);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeSyscalls43499_recv(
    JNIEnv *env, jclass, jint fd, jbyteArray buffer, jint flags) {
    if (!has_bytes(env, buffer)) return answer(-EINVAL);
    const jsize length = env->GetArrayLength(buffer);
    jbyte *const bytes = env->GetByteArrayElements(buffer, nullptr);
    if (bytes == nullptr) return answer(-ENOMEM);
    const long result = ghostlock::platform::syscalls43499::recv(
        static_cast<int>(fd), bytes, static_cast<std::size_t>(length), static_cast<int>(flags));
    env->ReleaseByteArrayElements(buffer, bytes, result >= 0 ? 0 : JNI_ABORT);
    return answer(result);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeSyscalls43499_close(
    JNIEnv *, jclass, jint fd) {
    return answer(ghostlock::platform::syscalls43499::close(static_cast<int>(fd)));
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeSyscalls43499_read(
    JNIEnv *env, jclass, jint fd, jbyteArray buffer) {
    if (!has_bytes(env, buffer)) return answer(-EINVAL);
    const jsize length = env->GetArrayLength(buffer);
    jbyte *const bytes = env->GetByteArrayElements(buffer, nullptr);
    if (bytes == nullptr) return answer(-ENOMEM);
    const long result = ghostlock::platform::syscalls43499::read(
        static_cast<int>(fd), bytes, static_cast<std::size_t>(length));
    env->ReleaseByteArrayElements(buffer, bytes, result >= 0 ? 0 : JNI_ABORT);
    return answer(result);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeSyscalls43499_write(
    JNIEnv *env, jclass, jint fd, jbyteArray buffer) {
    if (!has_bytes(env, buffer)) return answer(-EINVAL);
    const jsize length = env->GetArrayLength(buffer);
    jbyte *const bytes = env->GetByteArrayElements(buffer, nullptr);
    if (bytes == nullptr) return answer(-ENOMEM);
    const long result = ghostlock::platform::syscalls43499::write(
        static_cast<int>(fd), bytes, static_cast<std::size_t>(length));
    env->ReleaseByteArrayElements(buffer, bytes, JNI_ABORT);
    return answer(result);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeSyscalls43499_perfEventOpen(
    JNIEnv *env, jclass, jbyteArray attr, jint pid, jint cpu, jint group_fd, jlong flags) {
    if (!has_bytes(env, attr)) return answer(-EINVAL);
    const jsize length = env->GetArrayLength(attr);
    jbyte *const bytes = env->GetByteArrayElements(attr, nullptr);
    if (bytes == nullptr) return answer(-ENOMEM);
    const long result = ghostlock::platform::syscalls43499::perf_event_open(
        bytes, static_cast<int>(pid), static_cast<int>(cpu), static_cast<int>(group_fd),
        static_cast<unsigned long>(flags));
    env->ReleaseByteArrayElements(attr, bytes, JNI_ABORT);
    static_cast<void>(length);
    return answer(result);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeSyscalls43499_ioctl(
    JNIEnv *, jclass, jint fd, jlong request, jlong arg) {
    return answer(ghostlock::platform::syscalls43499::ioctl(
        static_cast<int>(fd), static_cast<unsigned long>(request),
        static_cast<std::uintptr_t>(arg)));
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeSyscalls43499_gettid(JNIEnv *, jclass) {
    return answer(ghostlock::platform::syscalls43499::gettid());
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeSyscalls43499_getpid(JNIEnv *, jclass) {
    return answer(ghostlock::platform::syscalls43499::getpid());
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeSyscalls43499_processVmWritev(
    JNIEnv *env, jclass, jint pid, jlong remote, jbyteArray buffer) {
    if (!has_bytes(env, buffer)) return answer(-EINVAL);
    const jsize length = env->GetArrayLength(buffer);
    if (length == 0) return answer(-EINVAL);
    jbyte *const bytes = env->GetByteArrayElements(buffer, nullptr);
    if (bytes == nullptr) return answer(-ENOMEM);
    const long result = ghostlock::platform::syscalls43499::process_vm_writev(
        static_cast<int>(pid), static_cast<std::uintptr_t>(remote),
        reinterpret_cast<const std::uint8_t *>(bytes), static_cast<std::size_t>(length));
    env->ReleaseByteArrayElements(buffer, bytes, JNI_ABORT);
    return answer(result);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeSyscalls43499_processVmReadv(
    JNIEnv *env, jclass, jint pid, jlong remote, jbyteArray buffer) {
    if (!has_bytes(env, buffer)) return answer(-EINVAL);
    const jsize length = env->GetArrayLength(buffer);
    if (length == 0) return answer(-EINVAL);
    jbyte *const bytes = env->GetByteArrayElements(buffer, nullptr);
    if (bytes == nullptr) return answer(-ENOMEM);
    const long result = ghostlock::platform::syscalls43499::process_vm_readv(
        static_cast<int>(pid), static_cast<std::uintptr_t>(remote),
        reinterpret_cast<std::uint8_t *>(bytes), static_cast<std::size_t>(length));
    env->ReleaseByteArrayElements(buffer, bytes, 0);
    return answer(result);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeSyscalls43499_usleep(
    JNIEnv *, jclass, jlong micros) {
    return answer(ghostlock::platform::syscalls43499::usleep(static_cast<std::uint64_t>(micros)));
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeSyscalls43499_clockGettimeNs(
    JNIEnv *, jclass, jint clock_id) {
    return answer(ghostlock::platform::syscalls43499::clock_gettime_ns(static_cast<int>(clock_id)));
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeSyscalls43499_pipe2(
    JNIEnv *env, jclass, jintArray out, jint flags) {
    if (out == nullptr || env->GetArrayLength(out) < 2) return answer(-EINVAL);
    int fds[2] = {-1, -1};
    const long result = ghostlock::platform::syscalls43499::pipe2(fds, static_cast<int>(flags));
    if (result < 0) return answer(result);
    env->SetIntArrayRegion(out, 0, 2, fds);
    return answer(0);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeSyscalls43499_schedSetaffinity(
    JNIEnv *, jclass, jint pid, jlong cpu_mask) {
    return answer(ghostlock::platform::syscalls43499::sched_setaffinity(
        static_cast<int>(pid), static_cast<std::uint64_t>(cpu_mask)));
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeSyscalls43499_schedSetattr(
    JNIEnv *env, jclass, jint tid, jbyteArray attr) {
    if (!has_bytes(env, attr)) return answer(-EINVAL);
    jbyte *const bytes = env->GetByteArrayElements(attr, nullptr);
    if (bytes == nullptr) return answer(-ENOMEM);
    const long result = ghostlock::platform::syscalls43499::sched_setattr(
        static_cast<int>(tid), bytes);
    env->ReleaseByteArrayElements(attr, bytes, JNI_ABORT);
    return answer(result);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeSyscalls43499_schedGetcpu(JNIEnv *, jclass) {
    return answer(ghostlock::platform::syscalls43499::sched_getcpu());
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeSyscalls43499_rseq(
    JNIEnv *env, jclass, jbyteArray rseq, jint flags, jint signature) {
    if (!has_bytes(env, rseq)) return answer(-EINVAL);
    const jsize length = env->GetArrayLength(rseq);
    jbyte *const bytes = env->GetByteArrayElements(rseq, nullptr);
    if (bytes == nullptr) return answer(-ENOMEM);
    const long result = ghostlock::platform::syscalls43499::rseq(
        bytes, static_cast<std::uint32_t>(length), static_cast<int>(flags),
        static_cast<std::uint32_t>(signature));
    env->ReleaseByteArrayElements(rseq, bytes, JNI_ABORT);
    return answer(result);
}
