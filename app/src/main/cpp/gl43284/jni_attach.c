#include <jni.h>

/**
 * @brief Register the 43284 log sink.
 * @note P6c will replace this with the per-DSO sink; for now the symbol is
 *       exported so System.loadLibrary and the Kotlin bridge can bind, while
 *       no writable process-global fd is introduced.
 * @return previous fd, always -1 until the sink is implemented.
 */
JNIEXPORT jint JNICALL
Java_com_ghostlock_app_log_NativeLogBridge_attachFd43284(JNIEnv *env, jobject thiz,
                                                         jint fd, jboolean debug) {
  (void)env;
  (void)thiz;
  (void)fd;
  (void)debug;
  return -1;
}
