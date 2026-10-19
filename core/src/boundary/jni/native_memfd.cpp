/*
 * Anonymous log channel for the finish chain.
 *
 * The root-side script appends to a memfd the app owns; the app drains it and
 * persists run-<epoch>.log. Nothing here touches a filesystem path.
 */
#include <jni.h>

#include <fcntl.h>
#include <linux/memfd.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>

extern "C" JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_NativeMemfd_createNative(JNIEnv * /*env*/, jobject /*thiz*/) {
#ifdef __NR_memfd_create
    const long fd = ::syscall(__NR_memfd_create, "ghostlock-log", 0u);
    return fd >= 0 ? static_cast<jint>(fd) : -errno;
#else
    return -ENOSYS;
#endif
}

extern "C" JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_NativeMemfd_readNative(JNIEnv *env, jobject /*thiz*/, jint fd,
                                                  jbyteArray buffer, jlong offset) {
    if (buffer == nullptr) {
        return -EINVAL;
    }
    const jsize length = env->GetArrayLength(buffer);
    if (length <= 0) {
        return 0;
    }
    jbyte *data = env->GetByteArrayElements(buffer, nullptr);
    if (data == nullptr) {
        return -ENOMEM;
    }
    const ssize_t read = ::pread(fd, data, static_cast<size_t>(length),
                                 static_cast<off_t>(offset));
    env->ReleaseByteArrayElements(buffer, data, 0);
    return read >= 0 ? static_cast<jint>(read) : -errno;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_NativeMemfd_writeNative(JNIEnv *env, jobject /*thiz*/, jint fd,
                                                   jbyteArray bytes, jlong offset) {
    if (bytes == nullptr) {
        return -EINVAL;
    }
    const jsize length = env->GetArrayLength(bytes);
    if (length <= 0) {
        return 0;
    }
    jbyte *data = env->GetByteArrayElements(bytes, nullptr);
    if (data == nullptr) {
        return -ENOMEM;
    }
    const ssize_t written = ::pwrite(fd, data, static_cast<size_t>(length),
                                     static_cast<off_t>(offset));
    env->ReleaseByteArrayElements(bytes, data, JNI_ABORT);
    return written >= 0 ? static_cast<jint>(written) : -errno;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_NativeMemfd_syncNative(JNIEnv * /*env*/, jobject /*thiz*/, jint fd) {
    return ::fdatasync(fd) == 0 ? 0 : -errno;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_NativeMemfd_closeNative(JNIEnv * /*env*/, jobject /*thiz*/, jint fd) {
    return ::close(fd) == 0 ? 0 : -errno;
}
