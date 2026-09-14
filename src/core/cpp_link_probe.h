#ifndef GHOSTLOCK_CPP_LINK_PROBE_H
#define GHOSTLOCK_CPP_LINK_PROBE_H

#ifdef __cplusplus
extern "C" {
#endif

/* CPP00 build-only C ABI probe. It is intentionally not called by the attack
 * control flow; the host test exercises STL allocation, RAII and errno. */
int ghostlock_cpp_link_probe(const char *text, int saved_errno)
#ifdef __cplusplus
    noexcept
#endif
    ;

#ifdef __cplusplus
}
#endif

#endif
