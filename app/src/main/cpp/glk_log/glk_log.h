#ifndef GLK_LOG_H
#define GLK_LOG_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Borrowed log callback; it must not call back into JNI or malloc.
 * @note The session owns the user pointer's lifetime; the callback is never
 *       invoked from inside the futex PI window.
 */
typedef struct {
  void (*write)(void *user, int level, const char *message);
  void *user;
} GlkLogSink;

/** @brief Descriptor kind detected once when a handle adopts the fd. */
typedef enum {
  GLK_FD_FILE = 0,
  GLK_FD_SOCKET = 1,
  GLK_FD_FIFO = 2,
} GlkFdKind;

/**
 * @brief Handle-private descriptor sink.
 * @note fd is owned by the sink; the Kotlin side keeps and closes its own fd.
 *       A write failure disables the sink and never closes it.
 */
typedef struct {
  int fd;
  GlkFdKind kind;
  atomic_bool disabled;
  atomic_bool in_pi;
} GlkFdSink;

/** @brief Detect kind and initialize an already-owned fd. */
void glk_fd_sink_init(GlkFdSink *sink, int fd);

/** @brief Disable and close the owned fd once; idempotent. */
void glk_fd_sink_close(GlkFdSink *sink);

/** @brief GlkLogSink.write-compatible single-line writer. */
void glk_fd_sink_write(void *user, int level, const char *message);

/** @brief Format one line into a fixed stack buffer and call sink->write. */
void glk_logf(GlkLogSink *sink, int level, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/** @brief Set/clear the PI-window drop flag on a GlkFdSink-backed sink. */
void glk_log_pi_enter(GlkLogSink *sink);
void glk_log_pi_exit(GlkLogSink *sink);

#endif
