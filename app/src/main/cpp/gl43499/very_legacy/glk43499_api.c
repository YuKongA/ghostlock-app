#include "../glk43499_api.h"

#include <stdlib.h>

struct Glk43499Handle {
  GlkSession outer;
  Glk43499Session inner;
};

Glk43499Handle *glk43499_handle_create(const Glk43499Target *target,
                                      const Glk43499Config *config,
                                      const Glk43499Env *env) {
  if (!target || !config || !env) {
    return NULL;
  }
  Glk43499Handle *handle = calloc(1, sizeof(*handle));
  if (!handle) {
    return NULL;
  }
  if (glk_session_create_43499(&handle->outer, &handle->inner, target, config,
                               env) != GLK_OK) {
    free(handle);
    return NULL;
  }
  return handle;
}

int glk43499_handle_run(Glk43499Handle *handle) {
  if (!handle) {
    return 1;
  }
  return run_exploit(&handle->inner);
}

Glk43499Session *glk43499_handle_session(Glk43499Handle *handle) {
  return handle ? &handle->inner : NULL;
}

void glk43499_handle_destroy(Glk43499Handle *handle) {
  if (!handle) {
    return;
  }
  glk_session_destroy(&handle->outer);
  free(handle);
}
