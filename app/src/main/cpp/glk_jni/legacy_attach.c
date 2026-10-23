#include <jni.h>

/**
 * @brief Register the legacy frame-boundary log sink.
 * @note P6c will replace this with the real sink; the symbol exists so
 *       libghostlock_jni.so loads and the Kotlin bridge binds.
 * @return previous fd, always -1 until the sink is implemented.
 */
JNIEXPORT jint JNICALL
Java_com_ghostlock_app_log_NativeLogBridge_attachFdLegacy(JNIEnv *env, jobject thiz,
                                                          jint fd, jboolean debug) {
  (void)env;
  (void)thiz;
  (void)fd;
  (void)debug;
  return -1;
}
