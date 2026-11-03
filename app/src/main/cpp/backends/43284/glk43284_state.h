#ifndef GLK43284_STATE_H
#define GLK43284_STATE_H

#include <stddef.h>
#include <stdint.h>

/** @brief Saved libc++ patch state for one 43284 run. */
typedef struct {
  const char *lib;
  uint64_t tramp_aligned;
  uint8_t tramp_orig[16];
  uint64_t shell_off;
  uint32_t shell_padded;
  char shell_orig[1024];
  int valid;
} Glk43284PatchRestore;

/** @brief Caller-owned 43284 state; no process-wide instance. */
typedef struct {
  int encap_port;
  int sender_port;
  uint32_t spi;
  uint32_t seq;
  uint8_t aes_key[32];
  uint8_t *payload;
  size_t payload_len;
  char *ko_target;
  Glk43284PatchRestore restore;
} Glk43284State;

int glk43284_state_init(Glk43284State *st, int encap_port, int sender_port,
                        uint32_t spi, const uint8_t aes_key[32]);
int glk43284_run_state(Glk43284State *st);
void glk43284_state_cleanup(Glk43284State *st);

#endif
