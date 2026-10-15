#pragma once
/** @file support/log.hpp
 *  The one logging landing: a line writer with no dependencies beyond libc.
 *
 *  Why it is this small (user ruling 2026-10-14): the previous implementation pulled in spdlog, an
 *  asynchronous logger with a thread pool, formatters and sinks. The tree uses exactly one of its
 *  features -- "one line, with the numbers in it" -- and pays for the rest in build time, binary
 *  size, and a dependency whose failure mode on the device was a lost last line. What is left here
 *  is a FILE pointer, a level, a clock and one mutex; the whole thing is a header a reader can
 *  finish.
 *
 *  What did NOT change: the call surface. log(level, what) and its five overloads keep their exact
 *  signatures, so no call site moved, and the line format is still
 *  HH:MM:SS.mmm [L] message -- the shape the device logs and the greps around them were built on.
 *
 *  What is different, stated plainly: writes are synchronous. The old logger owned a worker thread
 *  because a blocking write inside the race window would have changed what was being measured; that
 *  window still forbids logging (pi_window.hpp), so the thread bought nothing where it was thought
 *  to matter, and cost the last line on every exit path that did not reach close().
 *
 *  Levels come from the profile through logging::open(), never from here: this file defines no
 *  policy, no path and no level of its own.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string_view>

namespace ghostlock::support {
    /** The levels a line can carry. Printed as one letter each, as the old format did. */
    enum class Level : std::uint8_t {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warn = 3,
        Error = 4,
        Critical = 5,
        Off = 6,
    };

    /** The one spelling of a level's letter; the letter is what appears in the line. */
    [[nodiscard]] constexpr char level_letter(Level level) noexcept {
        switch (level) {
            case Level::Trace: return 'T';
            case Level::Debug: return 'D';
            case Level::Info: return 'I';
            case Level::Warn: return 'W';
            case Level::Error: return 'E';
            case Level::Critical: return 'C';
            case Level::Off: return '-';
        }
        return '?';
    }

    /** A level from its name, as the profile spells it: trace/debug/info/warn/error/critical/off.
     *  An unknown name is Off, which is a named answer rather than a guess. */
    [[nodiscard]] inline Level level_of(std::string_view name) noexcept {
        if (name == "trace") {
            return Level::Trace;
        }
        if (name == "debug") {
            return Level::Debug;
        }
        if (name == "info") {
            return Level::Info;
        }
        if (name == "warn" || name == "warning") {
            return Level::Warn;
        }
        if (name == "error") {
            return Level::Error;
        }
        if (name == "critical") {
            return Level::Critical;
        }
        return Level::Off;
    }

    namespace logging_detail {
        /** One line's room. A line longer than this is cut, not refused: losing a tail is better
         *  than losing the line. */
        inline constexpr std::size_t kLineBytes = 1024U;
        /** The message's room inside a line (after the timestamp and the level letter). */
        inline constexpr std::size_t kBodyBytes = kLineBytes - 32U;

        /** The landing's whole state: opened or not, where to, and what to keep. */
        struct State final {
            std::mutex mutex{};
            std::FILE *sink = nullptr;
            Level minimum = Level::Info;
        };

        /** The one instance. A function-local static, so it is built on first use and no other
         *  translation unit can name it. */
        [[nodiscard]] inline State &state() noexcept {
            static State instance;
            return instance;
        }

        /** Write one already-formatted body as a line, under the landing's mutex.
         *  @note The timestamp is taken here rather than by the caller: a line's time is when it
         *        was written, not when its message was built. */
        inline void write_line(Level level, const char *body) noexcept {
            State &s = state();
            std::lock_guard<std::mutex> guard(s.mutex);
            if (s.sink == nullptr ||
                static_cast<unsigned>(level) < static_cast<unsigned>(s.minimum)) {
                return;
            }
            std::timespec now{};
            (void)::clock_gettime(CLOCK_REALTIME, &now);
            std::tm broken{};
            const std::time_t seconds = static_cast<std::time_t>(now.tv_sec);
            (void)::localtime_r(&seconds, &broken);
            char line[kLineBytes];
            const int head = std::snprintf(line, sizeof(line), "%02d:%02d:%02d.%03ld [%c] ",
                                           broken.tm_hour, broken.tm_min, broken.tm_sec,
                                           static_cast<long>(now.tv_nsec / 1000000L),
                                           level_letter(level));
            std::size_t at = head > 0 ? static_cast<std::size_t>(head) : 0U;
            for (const char *cursor = body; *cursor != '\0' && at + 2U < sizeof(line); ++cursor) {
                line[at++] = *cursor;
            }
            line[at++] = '\n';
            (void)std::fwrite(line, 1U, at, s.sink);
            if (static_cast<unsigned>(level) >= static_cast<unsigned>(Level::Warn)) {
                /* Warnings and worse are flushed as they are written: they are the lines a run
                 *  that dies without reaching close() must still leave behind. */
                (void)std::fflush(s.sink);
            }
        }

        /** Format a body with snprintf into a stack buffer, then write it. */
        template <class... Args>
        inline void write_formatted(Level level, const char *format, Args... args) noexcept {
            char body[kBodyBytes];
            (void)std::snprintf(body, sizeof(body), format, args...);
            write_line(level, body);
        }

        /** Copy a view into a NUL-terminated stack buffer, cutting rather than overflowing. */
        inline void copy_cstr(const std::string_view text, char *out, const std::size_t room) noexcept {
            const std::size_t bytes = text.size() < room - 1U ? text.size() : room - 1U;
            std::memcpy(out, text.data(), bytes);
            out[bytes] = '\0';
        }
    }  // namespace logging_detail

    /** A number that is an address, or any word read as hex. */
    struct Hex final {
        std::uint64_t value;
    };

    /** A string that is data, not a level or a label: a policy name, a domain, a refusal word.
     *  Wrapped rather than passed as a bare string_view on purpose -- a bare one would be ambiguous
     *  against the numeric overloads, and an ambiguous call is one a caller stops making. */
    struct Text final {
        std::string_view value;
    };

    /** Start and stop the logger. Not lines, so not log(). */
    namespace logging {
        /**
         *  Start logging.
         *
         *  @param level_name trace/debug/info/warn/error/critical/off, from the profile.
         *  @param file_path  where lines go; empty means stderr.
         *  @return true when lines will be written; false is a named refusal (an unopenable file).
         *  @note Idempotent: the first call wins, later ones are no-ops that answer true.
         */
        [[nodiscard]] inline bool open(std::string_view level_name,
                                       std::string_view file_path) noexcept {
            logging_detail::State &s = logging_detail::state();
            std::lock_guard<std::mutex> guard(s.mutex);
            if (s.sink != nullptr) {
                return true;
            }
            s.minimum = level_of(level_name);
            if (file_path.empty()) {
                s.sink = stderr;
                return true;
            }
            char path[512];
            logging_detail::copy_cstr(file_path, path, sizeof(path));
            std::FILE *file = std::fopen(path, "ab");
            if (file == nullptr) {
                return false;
            }
            s.sink = file;
            return true;
        }

        /**
         *  Flush and stop. Call once, after the last line, before exiting.
         *
         *  @return true when a sink was closed; false when none was open, which is not an error.
         *  @note The convention for the last line is unchanged: exit-code carries the PROCESS exit
         *        code, written by the caller's finish() helper immediately before this call, so the
         *        last line of the file and the process status agree by construction.
         */
        [[nodiscard]] inline bool close() noexcept {
            logging_detail::State &s = logging_detail::state();
            std::lock_guard<std::mutex> guard(s.mutex);
            if (s.sink == nullptr) {
                return false;
            }
            (void)std::fflush(s.sink);
            if (s.sink != stderr) {
                (void)std::fclose(s.sink);
            }
            s.sink = nullptr;
            return true;
        }

        /** @return true when lines at this level would be written. */
        [[nodiscard]] inline bool enabled(Level level) noexcept {
            logging_detail::State &s = logging_detail::state();
            std::lock_guard<std::mutex> guard(s.mutex);
            return s.sink != nullptr &&
                   static_cast<unsigned>(level) >= static_cast<unsigned>(s.minimum);
        }
    }  // namespace logging

    /** One line.
     *  @param level_name debug / info / warn / error, chosen by the caller.
     *  @param what What is about to happen or has happened, in the caller's own words. */
    inline void log(std::string_view level_name, std::string_view what) noexcept {
        const Level level = level_of(level_name);
        if (!logging::enabled(level)) {
            return;
        }
        char body[logging_detail::kBodyBytes];
        logging_detail::copy_cstr(what, body, sizeof(body));
        logging_detail::write_line(level, body);
    }

    /** One line with one number: a return code, an errno, a count. */
    inline void log(std::string_view level_name, std::string_view what,
                    std::int64_t value) noexcept {
        const Level level = level_of(level_name);
        if (!logging::enabled(level)) {
            return;
        }
        char head[logging_detail::kBodyBytes];
        logging_detail::copy_cstr(what, head, sizeof(head));
        logging_detail::write_formatted(level, "%s value=%lld", head, static_cast<long long>(value));
    }

    /** One line with two numbers: a judgement pair such as (ret, errno). */
    inline void log(std::string_view level_name, std::string_view what, std::int64_t first,
                    std::int64_t second) noexcept {
        const Level level = level_of(level_name);
        if (!logging::enabled(level)) {
            return;
        }
        char head[logging_detail::kBodyBytes];
        logging_detail::copy_cstr(what, head, sizeof(head));
        logging_detail::write_formatted(level, "%s a=%lld b=%lld", head,
                                        static_cast<long long>(first),
                                        static_cast<long long>(second));
    }

    /** One line with four numbers: a stage's reading set, still one line so it stays one grep
     *  entry. More values are added as overloads -- never as a second function name, because a
     *  caller that has to pick a name is a caller that leaves numbers out. */
    inline void log(std::string_view level_name, std::string_view what, std::int64_t first,
                    std::int64_t second, std::int64_t third, std::int64_t fourth) noexcept {
        const Level level = level_of(level_name);
        if (!logging::enabled(level)) {
            return;
        }
        char head[logging_detail::kBodyBytes];
        logging_detail::copy_cstr(what, head, sizeof(head));
        logging_detail::write_formatted(level, "%s a=%lld b=%lld c=%lld d=%lld", head,
                                        static_cast<long long>(first),
                                        static_cast<long long>(second),
                                        static_cast<long long>(third),
                                        static_cast<long long>(fourth));
    }

    /** One line with a string attached: a name, a policy, a refusal word. */
    inline void log(std::string_view level_name, std::string_view what, Text text) noexcept {
        const Level level = level_of(level_name);
        if (!logging::enabled(level)) {
            return;
        }
        char head[logging_detail::kBodyBytes];
        logging_detail::copy_cstr(what, head, sizeof(head));
        char tail[logging_detail::kBodyBytes];
        logging_detail::copy_cstr(text.value, tail, sizeof(tail));
        logging_detail::write_formatted(level, "%s %s", head, tail);
    }

    /** One line with an address, printed as hex. */
    inline void log(std::string_view level_name, std::string_view what, Hex hex) noexcept {
        const Level level = level_of(level_name);
        if (!logging::enabled(level)) {
            return;
        }
        char head[logging_detail::kBodyBytes];
        logging_detail::copy_cstr(what, head, sizeof(head));
        logging_detail::write_formatted(level, "%s 0x%016llx", head,
                                        static_cast<unsigned long long>(hex.value));
    }
}  // namespace ghostlock::support
