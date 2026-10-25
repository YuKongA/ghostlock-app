#include <jni.h>

#include "../../general/gl_log.h"

/**
 * @brief Register the 43499 log sink.
 * @note 43499 uses the standard general/gl_log fd sink. The fd is duplicated;
 *       the Kotlin-side original remains owned by LogFile.
 * @return owned duplicate fd, or -1 when disabled/dup failed.
 */
JNIEXPORT jint JNICALL
Java_com_ghostlock_app_log_NativeLogBridge_attachFd43499(JNIEnv *env, jobject thiz,
                                                         jint fd, jboolean debug) {
  (void)env;
  (void)thiz;
  return gl_log_attach_fd((int)fd, debug != JNI_FALSE,
                          debug != JNI_FALSE ? GL_LOG_DEBUG : GL_LOG_INFO);
}
