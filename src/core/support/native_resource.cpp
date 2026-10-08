#include "support/native_resource.hpp"

#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <utility>

namespace ghostlock::support {
    UniqueFd::~UniqueFd() noexcept { reset(); }

    UniqueFd::UniqueFd(UniqueFd &&other) noexcept : fd_(other.release()) {
    }

    UniqueFd &UniqueFd::operator=(UniqueFd &&other) noexcept {
        if (this != &other) reset(other.release());
        return *this;
    }

    int32_t UniqueFd::release() noexcept { return std::exchange(fd_, -1); }

    int32_t UniqueFd::release_to_process_lifetime(std::string_view reason) noexcept {
        (void) reason;
        return release();
    }

    void UniqueFd::reset(int32_t fd) noexcept {
        const int32_t old = std::exchange(fd_, fd);
        if (old >= 0) {
            const int32_t saved_errno = errno;
            close(old);
            errno = saved_errno;
        }
    }

    MappedRegion::~MappedRegion() noexcept { reset(); }

    MappedRegion::MappedRegion(MappedRegion &&other) noexcept
        : address_(other.release()), size_(std::exchange(other.size_, 0)) {
    }

    MappedRegion &MappedRegion::operator=(MappedRegion &&other) noexcept {
        if (this != &other) {
            reset();
            size_ = other.size_;
            address_ = other.release();
            other.size_ = 0;
        }
        return *this;
    }

    Result<MappedRegion> MappedRegion::map_anonymous(std::size_t size,
                                                     int32_t protection) {
        void *address = mmap(nullptr, size, protection, MAP_PRIVATE | MAP_ANONYMOUS,
                             -1, 0);
        if (address == MAP_FAILED) {
            return std::unexpected(SysError::from_errno());
        }
        return MappedRegion(address, size);
    }

    bool MappedRegion::valid() const noexcept {
        return address_ != nullptr && address_ != MAP_FAILED && size_ != 0;
    }

    std::span<std::byte> MappedRegion::bytes() const noexcept {
        return valid()
                   ? std::span(static_cast<std::byte *>(address_), size_)
                   : std::span<std::byte>();
    }

    void *MappedRegion::release() noexcept {
        return std::exchange(address_, nullptr);
    }

    void MappedRegion::reset() noexcept {
        void *old = std::exchange(address_, nullptr);
        const std::size_t old_size = std::exchange(size_, 0);
        if (old && old != MAP_FAILED && old_size) {
            const int32_t saved_errno = errno;
            munmap(old, old_size);
            errno = saved_errno;
        }
    }

    PthreadOwner::~PthreadOwner() noexcept {
        request_stop();
        detach_if_joinable();
    }

    PthreadOwner::PthreadOwner(PthreadOwner &&other) noexcept
        : thread_(other.thread_),
          state_(std::exchange(other.state_, State::Empty)),
          stop_(std::exchange(other.stop_, nullptr)),
          stop_argument_(std::exchange(other.stop_argument_, nullptr)) {
    }

    PthreadOwner &PthreadOwner::operator=(PthreadOwner &&other) noexcept {
        if (this != &other) {
            request_stop();
            detach_if_joinable();
            thread_ = other.thread_;
            state_ = std::exchange(other.state_, State::Empty);
            stop_ = std::exchange(other.stop_, nullptr);
            stop_argument_ = std::exchange(other.stop_argument_, nullptr);
        }
        return *this;
    }

    int32_t PthreadOwner::start(void *(*entry)(void *), void *argument,
                        ThreadStopCallback stop,
                        void *stop_argument)
    noexcept
 {
  if (joinable() || !entry) return EINVAL;
  const int32_t error = pthread_create(&thread_, nullptr, entry, argument);
  if (!error) {
    state_ = State::Running;
    stop_ = stop;
    stop_argument_ = stop_argument;
  }
  return error;
}

    void PthreadOwner::request_stop() noexcept {
        if (state_ != State::Running) return;
        state_ = State::StopRequested;
        if (stop_) stop_(stop_argument_);
    }

    int32_t PthreadOwner::join(void **result) noexcept {
        if (!joinable()) return EINVAL;
        const int32_t error = pthread_join(thread_, result);
        if (!error) {
            state_ = State::Joined;
            stop_ = nullptr;
            stop_argument_ = nullptr;
        }
        return error;
    }

    int32_t PthreadOwner::detach() noexcept {
        if (!joinable()) return EINVAL;
        const int32_t error = pthread_detach(thread_);
        if (!error) {
            state_ = State::Detached;
            stop_ = nullptr;
            stop_argument_ = nullptr;
        }
        return error;
    }

    pthread_t PthreadOwner::release() noexcept {
        const pthread_t released = thread_;
        state_ = State::Empty;
        stop_ = nullptr;
        stop_argument_ = nullptr;
        return released;
    }

    bool PthreadOwner::joinable() const noexcept {
        return state_ == State::Running || state_ == State::StopRequested;
    }

    void PthreadOwner::detach_if_joinable() noexcept {
        if (joinable()) (void) detach();
    }

    ChildProcess::~ChildProcess() noexcept { cleanup_if_running(); }

    ChildProcess::ChildProcess(ChildProcess &&other) noexcept
        : pid_(std::exchange(other.pid_, -1)),
          state_(std::exchange(other.state_, State::Empty)) {
    }

    ChildProcess &ChildProcess::operator=(ChildProcess &&other) noexcept {
        if (this != &other) {
            cleanup_if_running();
            pid_ = std::exchange(other.pid_, -1);
            state_ = std::exchange(other.state_, State::Empty);
        }
        return *this;
    }

    pid_t ChildProcess::release_to_handoff() noexcept {
        if (!valid()) return -1;
        state_ = State::Transferred;
        return std::exchange(pid_, -1);
    }

    int32_t ChildProcess::terminate_and_wait(int32_t signal_number) noexcept {
        if (!valid()) return EINVAL;
        const pid_t owned = pid_;
        if (kill(owned, signal_number) != 0 && errno != ESRCH) return errno;
        while (waitpid(owned, nullptr, 0) < 0) {
            if (errno != EINTR) return errno;
        }
        pid_ = -1;
        state_ = State::Reaped;
        return 0;
    }

    void ChildProcess::mark_reaped() noexcept {
        if (!valid()) return;
        pid_ = -1;
        state_ = State::Reaped;
    }

    void ChildProcess::cleanup_if_running() noexcept {
        if (valid()) (void) terminate_and_wait(SIGKILL);
    }
} // namespace ghostlock::support
