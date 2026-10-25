#ifndef GLK43284_API_H
#define GLK43284_API_H

#include <stdint.h>

#include "glk43284_state.h"

#if defined(__GNUC__)
#define GLK43284_API __attribute__((visibility("default")))
#else
#define GLK43284_API
#endif

/** @brief Immutable per-run 43284 parameters. */
typedef struct {
  int encap_port;
  int sender_port;
  uint32_t spi;
  uint8_t aes_key[32];
} Glk43284Config;

/** @brief Opaque caller-owned 43284 handle; no process-wide instance. */
typedef struct Glk43284Handle Glk43284Handle;

/**
 * @brief Initialize state and allocate one 43284 handle.
 * @return Handle, or NULL on invalid config/allocation failure.
 */
GLK43284_API Glk43284Handle *glk43284_handle_create(const Glk43284Config *config);

/** @brief Run the 43284 exploit; returns its process-style result code. */
GLK43284_API int glk43284_handle_run(Glk43284Handle *handle);

/** @brief Restore/cleanup and free a handle. */
GLK43284_API void glk43284_handle_destroy(Glk43284Handle *handle);

#endif
