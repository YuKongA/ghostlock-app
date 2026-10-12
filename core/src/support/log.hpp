#pragma once
/** @file support/log.hpp
 *  The one logging seam: an asynchronous spdlog, reached through one function name.
 *
 *  Why this exists: the race window has no log of its own, so a diagnosis had to be
 *  reconstructed from scattered printfs after the fact. A line before a step starts is worth
 *  more than any amount of post-hoc reading, so every major step announces itself here before
 *  it runs.
 *
 *  One name, overloaded. A caller writes log(level, what) and adds numbers as it has them;
 *  there is no vocabulary of different log_* verbs to pick from, because picking is where the
 *  numbers get left out. Lifecycle is not a line, so it lives in the logging namespace: open()
 *  once, close() once.
 *
 *  Asynchronous on purpose: the race window is a timing window, and a logger that blocks on a
 *  file write inside it would change what is being measured. (Nothing calls this from inside PI:
 *  pi_window.hpp:12-21 forbids logging and stdio there, and the readings PI produces are carried
 *  out in shared_ fields and printed here once the last actor has stopped.)
 *
 *  Levels and the sink come from the profile, not from here: no value is hard-coded.
 */
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace ghostlock::support {
    /** The logger's registered name: what log() looks up. */
    inline constexpr const char* kLogName = "ghostlock";

    /** A number that is an address, or any word read as hex. */
    struct Hex final {
        std::uint64_t value;
    };

    /** A string that is data, not a level or a label: a policy name, a domain, a refusal word.
     *  Wrapped rather than passed as a bare string_view on purpose -- a bare one would be
     *  ambiguous against the numeric overloads, and an ambiguous call is one a caller stops
     *  making. The wrapper keeps the call site explicit. */
    struct Text final {
        std::string_view value;
    };

    /** Start and stop the logger. Not lines, so not log(). */
    namespace logging {
        /** Start the asynchronous logger. Idempotent: the first call wins, later ones are no-ops.
         *  @param level_name One of spdlog's level names, from the profile.
         *  @param file_path The log file; empty means stderr only.
         *  @return true when a logger is available afterwards; false is a named refusal. */
        [[nodiscard]] inline bool open(std::string_view level_name,
                                       std::string_view file_path) noexcept {
            if (spdlog::get(kLogName) != nullptr) {
                return true;
            }
            try {
                spdlog::init_thread_pool(8192U, 1U);
                std::shared_ptr<spdlog::logger> logger =
                        file_path.empty()
                                ? spdlog::stderr_color_mt<spdlog::async_factory>(kLogName)
                                : spdlog::basic_logger_mt<spdlog::async_factory>(
                                          kLogName, std::string(file_path));
                logger->set_level(spdlog::level::from_str(std::string(level_name)));
                logger->set_pattern("%Y-%m-%d %H:%M:%S.%e [%t] [%l] %v");
                logger->flush_on(spdlog::level::warn);
                spdlog::set_default_logger(logger);
                return true;
            } catch (const std::exception&) {
                return false;
            }
        }

        /** Flush the queue and stop the logger. Call once, after the last line, before exiting:
         *  an asynchronous logger writes on its own thread, so without this the last lines -- the
         *  verdict, the exit code, the first failure -- can still be in the queue at exit.
         *  @return true when a logger was flushed; false when none was started, which is not an
         *          error. Idempotent.
         *  @note The convention for that last line: `exit-code` carries the PROCESS exit code
         *        (0, 4, 7, 9, ...) -- the value the caller returns -- NOT a Lua verdict and not a
         *        step outcome. The caller's finish() helper writes it immediately before this
         *        call, so `tail -1` and the process status agree by construction. */
        [[nodiscard]] inline bool close() noexcept {
            const std::shared_ptr<spdlog::logger> logger = spdlog::get(kLogName);
            if (logger == nullptr) {
                return false;
            }
            try {
                /* Drain the queue BEFORE the shutdown: shutdown() stops the worker and drops
                 * whatever it had not consumed, and flush_on(warn) never fires for the info
                 * lines that matter here -- which is exactly how the last line (exit-code) went
                 * missing on the device while every earlier line landed. The comment above this
                 * function promised the flush; now the code does it. */
                logger->flush();
                spdlog::shutdown();
                return true;
            } catch (const std::exception&) {
                return false;
            }
        }
    }  // namespace logging

    /** One line.
     *  @param level_name debug / info / warn / error, chosen by the caller.
     *  @param what What is about to happen or has happened, in the caller's own words. */
    inline void log(std::string_view level_name, std::string_view what) noexcept {
        if (spdlog::get(kLogName) == nullptr) {
            return;
        }
        spdlog::log(spdlog::level::from_str(std::string(level_name)), "{}", what);
    }

    /** One line with one number: a return code, an errno, a count. */
    inline void log(std::string_view level_name, std::string_view what,
                    std::int64_t value) noexcept {
        if (spdlog::get(kLogName) == nullptr) {
            return;
        }
        spdlog::log(spdlog::level::from_str(std::string(level_name)), "{} value={}", what, value);
    }

    /** One line with two numbers: a judgement pair such as (ret, errno). */
    inline void log(std::string_view level_name, std::string_view what, std::int64_t first,
                    std::int64_t second) noexcept {
        if (spdlog::get(kLogName) == nullptr) {
            return;
        }
        spdlog::log(spdlog::level::from_str(std::string(level_name)), "{} a={} b={}", what, first,
                    second);
    }

    /** One line with four numbers: a stage's reading set, still one line so it stays one
     *  grep entry. More values are added as overloads -- never as a second function name,
     *  because a caller that has to pick a name is a caller that leaves numbers out. */
    inline void log(std::string_view level_name, std::string_view what, std::int64_t first,
                    std::int64_t second, std::int64_t third, std::int64_t fourth) noexcept {
        if (spdlog::get(kLogName) == nullptr) {
            return;
        }
        spdlog::log(spdlog::level::from_str(std::string(level_name)), "{} a={} b={} c={} d={}",
                    what, first, second, third, fourth);
    }

    /** One line with a string attached: a name, a policy, a refusal word. */
    inline void log(std::string_view level_name, std::string_view what, Text text) noexcept {
        if (spdlog::get(kLogName) == nullptr) {
            return;
        }
        spdlog::log(spdlog::level::from_str(std::string(level_name)), "{} {}", what,
                    text.value);
    }

    /** One line with an address, printed as hex. */
    inline void log(std::string_view level_name, std::string_view what, Hex hex) noexcept {
        if (spdlog::get(kLogName) == nullptr) {
            return;
        }
        spdlog::log(spdlog::level::from_str(std::string(level_name)), "{} 0x{:016x}", what,
                    hex.value);
    }
}  // namespace ghostlock::support
