#pragma once
/** @file jni/jni_context.hpp
 *  The parameterised context the six attack functions run on.
 *
 *  Every value the six cmp_disasm targets used to read from process-wide state
 *  (the session singleton, the profile accessors, the environment probes) is a
 *  field here instead. The caller -- Kotlin, through the three JNI entries --
 *  fills it before a run and reads its outcome afterwards. Nothing in this
 *  header reads files, environment or globals.
 *
 *  SUPERSEDED IN PART. This header was written from the C++ tree before the six targets
 *  were re-sourced from the pure-C original. Three of its types now have equivalents there,
 *  and a fourth group is the only home of the environment probes, so the file is kept and
 *  marked rather than deleted:
 *    WriteMode and WriteRequestParams  -> memory/write_request.hpp (WriteMode, WriteRequest)
 *    RaceWords                         -> jni/race_threads.hpp (race::RaceWords)
 *    ExecutionTuning                   -> partly RaceTuning and OffsetsView; four fields
 *                                         (race_route_wait_ms, race_state_poll_interval_us,
 *                                         select_consumer_max_calls, main_cpu) have no
 *                                         counterpart yet and are not to be dropped without
 *                                         a ruling
 *    EnvironmentFacts                  -> still the only place for the three probes
 *  An earlier version of this note claimed assertions at the bottom of the file; there are
 *  none, and none are being added here, because the authority for the crossed structures is
 *  now the headers named above.
 */
#include <cstddef>
#include <cstdint>

namespace ghostlock::jni {
    /** The three write modes, spelled once, used by both sides. */
    enum class WriteMode : std::int32_t {
        Disabled = 0,
        Zero = 1,
        Credential = 2,
    };

    /** One kernel write, parameterised. Mirrors memory::WriteRequest field for field. */
    struct WriteRequestParams final {
        std::uintptr_t target = 0U;
        WriteMode mode = WriteMode::Disabled;
        bool preserve_child = false;
    };

    /** The tuning the run is allowed to use, taken from the caller, never read here. */
    struct ExecutionTuning final {
        std::uint32_t race_route_wait_ms = 0U;
        std::uint32_t race_state_poll_interval_us = 0U;
        std::uint32_t select_consumer_max_calls = 0U;
        std::uint32_t select_consumer_burst_calls = 0U;
        std::uint32_t consumer_nice_period = 19U;
        std::uint32_t main_cpu = 0U;
        std::uint32_t consumer_cpu = 0U;
        bool has_compact_waiter = false;
    };

    /** What the caller probed before the run, so no probe happens inside it. */
    struct EnvironmentFacts final {
        bool selinux_off = false;
        bool enforce_readable = false;
        bool has_seccomp = false;
    };

    /** The futex words one attempt uses; the caller owns the storage. */
    struct RaceWords final {
        std::uint32_t *wait = nullptr;
        std::uint32_t *target = nullptr;
        std::uint32_t *chain = nullptr;
    };
}  // namespace ghostlock::jni
