#pragma once
/** @file jni/jni_platform.hpp
 *  The two platform calls the race entries need, in one header.
 *
 *  Both come from the pure-C original:
 *
 *    disable_rseq_for_thread  (bddfea4:src/core/util.c:165) is an empty function in the
 *      original -- three lines whose body is return;. It is kept as a no-op here so the
 *      two call sites in the entries stay where the original had them, and so the day the
 *      action matters again there is one place to put it.
 *
 *    pin_to_core              (bddfea4:src/core/kernelsnitch/utils.h:139) is seven lines:
 *      CPU_ZERO, CPU_SET, sched_setaffinity. The original lived under kernelsnitch, which
 *      this tree no longer carries, so the body is written out here instead of included.
 *      The value it takes used to be a compile-time constant at each call site (CORE for
 *      the main thread, CONSUMER_CORE for the consumer); both now arrive as tuning fields.
 *
 *  Nothing here reads a file, an environment variable or a global. Both take what they need.
 */
#include <cstddef>
#include <sched.h>

namespace ghostlock::jni {
    /**
     *  A no-op, kept because the original had a call site for it.
     *
     *  @note The original body was return; -- this is not a simplification, it is a copy.
     */
    inline void platform_disable_rseq_for_thread() noexcept {
        return;
    }

    /**
     *  Pin the calling thread to one cpu.
     *
     *  @param core  the cpu index; taken from tuning, never a compile-time constant.
     *  @note The setaffinity result is not checked here, matching the original, which called
     *        it through its SYSCHK wrapper at the call site rather than inside.
     */
    inline void platform_pin_to_core(std::size_t core) noexcept {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core, &cpuset);
        (void) ::sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
    }
}  // namespace ghostlock::jni
