#ifndef GLK43499_API_H
#define GLK43499_API_H

#include "../glk_session.h"

#if defined(__GNUC__)
#define GLK43499_API __attribute__((visibility("default")))
#else
#define GLK43499_API
#endif

/** @brief Opaque caller-owned 43499 handle; no process-wide instance. */
typedef struct Glk43499Handle Glk43499Handle;

/**
 * @brief Allocate and initialize one 43499 handle.
 * @return Handle, or NULL on invalid parameters/allocation failure.
 * @note The handle owns the session; the caller must destroy it exactly once.
 */
GLK43499_API Glk43499Handle *glk43499_handle_create(
    const Glk43499Target *target, const Glk43499Config *config,
    const Glk43499Env *env);

/**
 * @brief Build a handle from the fixed 5.15 test profile.
 * @param home Ignored during the Kotlin-profile migration.
 * @return Handle, or NULL when the fixed target/allocation fails.
 */
GLK43499_API Glk43499Handle *glk43499_handle_create_default(const char *home);

/** @brief Write the fixed root script for a caller-owned config. */
GLK43499_API void glk43499_write_root_script(const Glk43499Config *config);

/**
 * @brief Run the 43499 exploit on a handle.
 * @return Process-style result code (0 success, non-zero failure).
 */
GLK43499_API int glk43499_handle_run(Glk43499Handle *handle);

/** @brief Fill the temporary fixed 5.15 target (Kotlin profile migration). */
GLK43499_API int glk43499_fixed_target_515(Glk43499Target *target);

/** @brief Fill the temporary fixed 5.15 config (Kotlin profile migration). */
GLK43499_API void glk43499_fixed_config_515(Glk43499Config *config,
                                            const char *home);

/** @brief Borrow the session owned by a handle (temporary probe support). */
GLK43499_API Glk43499Session *glk43499_handle_session(Glk43499Handle *handle);

/**
 * @brief Destroy a handle and clear its session.
 * @note No threads/resources are freed here beyond the session teardown.
 */
GLK43499_API void glk43499_handle_destroy(Glk43499Handle *handle);

#endif
