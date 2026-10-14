/** @file jni/target_forwarders.cpp
 *  The spellings cmp_disasm resolves, in front of the implementations this experiment moved.
 *
 *  Why these exist: tools/cmp_disasm.py builds a set of names from llvm-objdump --demangle and
 *  then tests each candidate with a plain membership check (resolve, :192-196). A candidate
 *  matches only if the demangled name is equal to it, character for character. The candidates
 *  are written in the target list at :79-143.
 *
 *  Three of the six are reachable, because their candidates take void* -- the same shape as the
 *  entries this experiment wrote:
 *      ghostlock::race::waiter_thread(void*)
 *      ghostlock::race::owner_thread(void*)
 *      ghostlock::race::consumer_thread(void*)
 *  Each forwarder below carries that exact spelling and does nothing but hand the argument on.
 *
 *  The other three are NOT reachable, and this file does not pretend otherwise:
 *      run_main_route_threads      candidates take ghostlock::memory::WriteRequest, but the type
 *                                  in this tree is ghostlock::backend::cve_2026_43499::memory::
 *                                  WriteRequest (see memory/write_request.hpp:20). Matching the
 *                                  candidate would require a type that does not exist.
 *      do_kernel5_fake_lock_route  same reason.
 *      do_one_write                candidates are (WriteRequest const*, char const*) and a run of
 *                                  attack_write<...> instantiations; this experiment gave the
 *                                  function explicit value parameters instead.
 *  => A forwarder that used the candidate spelling would not compile; one that used the real type
 *     would not match. The difference is recorded rather than papered over with an alias type.
 */
#include <cstdint>

#include "jni/race_threads.hpp"

namespace ghostlock::race {
    /**
     *  The waiter entry under the spelling the target list uses.
     *
     *  @param argument  the race context, as jni::race::waiter_thread expects it.
     *  @return NULL, as the pthread entry point requires.
     */
    void *waiter_thread(void *argument) {
        return ghostlock::jni::race::waiter_thread(argument);
    }

    /**
     *  The owner entry under the spelling the target list uses.
     *
     *  @param argument  the race context.
     *  @return NULL.
     */
    void *owner_thread(void *argument) {
        return ghostlock::jni::race::owner_thread(argument);
    }

    /**
     *  The consumer entry under the spelling the target list uses.
     *
     *  @param argument  the race context.
     *  @return NULL.
     */
    void *consumer_thread(void *argument) {
        return ghostlock::jni::race::consumer_thread(argument);
    }
}  // namespace ghostlock::race
