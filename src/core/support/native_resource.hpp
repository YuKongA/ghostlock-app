#ifndef GHOSTLOCK_NATIVE_RESOURCE_HPP
#define GHOSTLOCK_NATIVE_RESOURCE_HPP

#include "support/native_result.hpp"

#include <pthread.h>
#include <sys/types.h>

#include <cstddef>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace ghostlock {
    class BorrowedFd final {
    public:
        constexpr BorrowedFd() noexcept = default;

        explicit constexpr BorrowedFd(int fd) noexcept : fd_(fd) {
        }

        [[nodiscard]] constexpr bool valid() const noexcept { return fd_ >= 0; }
        [[nodiscard]] constexpr int get() const noexcept { return fd_; }

    private:
        int fd_ = -1;
    };

    static_assert(std::is_trivially_copyable_v<BorrowedFd>);
    static_assert(sizeof(BorrowedFd) == sizeof(int));

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

        explicit constexpr UniqueFd(int fd) noexcept : fd_(fd) {
        }

        ~UniqueFd() noexcept;

        UniqueFd(const UniqueFd &) = delete;

        UniqueFd &operator=(const UniqueFd &) = delete;

        UniqueFd(UniqueFd &&other) noexcept;

        UniqueFd &operator=(UniqueFd &&other) noexcept;

        [[nodiscard]] constexpr bool valid() const noexcept { return fd_ >= 0; }
        [[nodiscard]] constexpr int get() const noexcept { return fd_; }

        [[nodiscard]] constexpr BorrowedFd borrow() const noexcept {
            return BorrowedFd(fd_);
        }

        [[nodiscard]] int release() noexcept;

        [[nodiscard]] int release_to_process_lifetime(
            std::string_view reason) noexcept;

        void reset(int fd = -1) noexcept;

    private:
        int fd_ = -1;
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
                                                                int protection);

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

        [[nodiscard]] int start(void *(*entry)(void *), void *argument,
                                ThreadStopCallback stop = nullptr,
                                void *stop_argument = nullptr) noexcept;

        void request_stop() noexcept;

        [[nodiscard]] int join(void **result = nullptr) noexcept;

        [[nodiscard]] int detach() noexcept;

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

        [[nodiscard]] int terminate_and_wait(int signal_number) noexcept;

        /* Marks a child the caller already reaped through waitpid(): the owner
   * drops the pid without signaling or waiting again. Idempotent; a no-op
   * when the owner no longer holds a running child. */
        void mark_reaped() noexcept;

    private:
        void cleanup_if_running() noexcept;

        pid_t pid_ = -1;
        State state_ = State::Empty;
    };
} // namespace ghostlock

#endif
