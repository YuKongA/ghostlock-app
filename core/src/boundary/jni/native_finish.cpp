/*
 * Finish parameter handoff JNI.
 *
 * Kotlin writes finish.params through this entry point. The root-side executor
 * (root_cmd) reads the same file; no shell script is generated in Kotlin.
 */
#include <jni.h>

#include <cstdio>

extern "C" JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_NativeFinish_writeFile(JNIEnv *env, jobject /*thiz*/,
                                                  jstring path, jstring content) {
    if (path == nullptr || content == nullptr) return -1;
    const char *native_path = env->GetStringUTFChars(path, nullptr);
    const char *native_content = env->GetStringUTFChars(content, nullptr);
    if (native_path == nullptr || native_content == nullptr) {
        if (native_path != nullptr) env->ReleaseStringUTFChars(path, native_path);
        if (native_content != nullptr) env->ReleaseStringUTFChars(content, native_content);
        return -1;
    }
    FILE *file = std::fopen(native_path, "wb");
    jint result = -1;
    if (file != nullptr) {
        const auto length = static_cast<size_t>(env->GetStringUTFLength(content));
        const size_t written = std::fwrite(native_content, 1, length, file);
        std::fflush(file);
        std::fclose(file);
        result = written == length ? 0 : -1;
    }
    env->ReleaseStringUTFChars(path, native_path);
    env->ReleaseStringUTFChars(content, native_content);
    return result;
}
