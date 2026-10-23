#include "glk43284_api.h"

#include <stdlib.h>

struct Glk43284Handle {
  Glk43284State state;
};

Glk43284Handle *glk43284_handle_create(const Glk43284Config *config) {
  if (!config) {
    return NULL;
  }
  Glk43284Handle *handle = calloc(1, sizeof(*handle));
  if (!handle) {
    return NULL;
  }
  if (glk43284_state_init(&handle->state, config->encap_port,
                          config->sender_port, config->spi,
                          config->aes_key) != 0) {
    free(handle);
    return NULL;
  }
  return handle;
}

int glk43284_handle_run(Glk43284Handle *handle) {
  if (!handle) {
    return 1;
  }
  return glk43284_run_state(&handle->state);
}

void glk43284_handle_destroy(Glk43284Handle *handle) {
  if (!handle) {
    return;
  }
  glk43284_state_cleanup(&handle->state);
  free(handle);
}
