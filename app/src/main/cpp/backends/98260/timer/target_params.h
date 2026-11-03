#ifndef GHOSTLOCK_BACKENDS_98260_TIMER_TARGET_PARAMS_H
#define GHOSTLOCK_BACKENDS_98260_TIMER_TARGET_PARAMS_H

/**
 * @file target_params.h
 * @brief Select the single compile-time CVE-2026-98260 target authority.
 *
 * @note CMake must define exactly one supported target. Runtime kernel-version
 *       guessing is intentionally absent so a helper cannot mix ABI facts.
 */

#if defined(GLK98260_TARGET_AVD_61) && defined(GLK98260_TARGET_A301SO_515)
#error "CVE-2026-98260 helper received more than one target selector"
#elif defined(GLK98260_TARGET_AVD_61)
#include "avd_61_params.h"
#elif defined(GLK98260_TARGET_A301SO_515)
#include "a301so_515_params.h"
#else
#error "CVE-2026-98260 helper requires an explicit target selector"
#endif

/** @brief Shared procfs control used to expose nonzero kallsyms to ksud. */
#define GLK98260_KPTR_RESTRICT_PATH "/proc/sys/kernel/kptr_restrict"

/** @brief Temporary value required by the kallsyms-aware late-load path. */
#define GLK98260_KPTR_RESTRICT_VISIBLE 0

#endif
