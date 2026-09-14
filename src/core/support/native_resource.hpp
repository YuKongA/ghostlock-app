#ifndef GHOSTLOCK_NATIVE_RESOURCE_HPP
#define GHOSTLOCK_NATIVE_RESOURCE_HPP

#include "support/native_result.hpp"

#include <pthread.h>
#include <sys/types.h>

#include <cstddef>
#include <span>

namespace ghostlock {

    class UniqueFd final {
    public:
        constexpr UniqueFd()

        noexcept =
        default;

        explicit constexpr UniqueFd(int fd)

        noexcept : fd_(fd) {}

        ~UniqueFd()

        noexcept;

        UniqueFd(const UniqueFd &) = delete;

        UniqueFd &operator=(const UniqueFd &) = delete;

        UniqueFd(UniqueFd &&other)

        noexcept;

        UniqueFd &operator=(UniqueFd &&other)

        noexcept;

        [[nodiscard]] constexpr bool valid() const

        noexcept {
            return fd_ >= 0;
        }

        [[nodiscard]] constexpr int get() const

        noexcept {
            return fd_;
        }

        [[nodiscard]] int release()

        noexcept;

        void reset(int fd = -1)

        noexcept;

    private:
        int fd_ = -1;
    };

    class MappedRegion final {
    public:
        constexpr MappedRegion()

        noexcept =
        default;

        MappedRegion(void *address, std::size_t size)

        noexcept
                : address_(address), size_(size) {
        }

        ~MappedRegion()

        noexcept;

        MappedRegion(const MappedRegion &) = delete;

        MappedRegion &operator=(const MappedRegion &) = delete;

        MappedRegion(MappedRegion &&other)

        noexcept;

        MappedRegion &operator=(MappedRegion &&other)

        noexcept;

        [[nodiscard]] static Result <MappedRegion> map_anonymous(std::size_t size,
                int protection);

        [[nodiscard]] bool valid() const

        noexcept;

        [[nodiscard]] void *data() const

        noexcept {
            return address_;
        }

        [[nodiscard]] std::size_t size() const

        noexcept {
            return size_;
        }

        [[nodiscard]] std::span <std::byte> bytes() const

        noexcept;

        [[nodiscard]] void *release()

        noexcept;

        void reset()

        noexcept;

    private:
        void *address_ = nullptr;
        std::size_t size_ = 0;
    };

    class PthreadOwner final {
    public:
        constexpr PthreadOwner()

        noexcept =
        default;

        ~PthreadOwner()

        noexcept;

        PthreadOwner(const PthreadOwner &) = delete;

        PthreadOwner &operator=(const PthreadOwner &) = delete;

        PthreadOwner(PthreadOwner &&other)

        noexcept;

        PthreadOwner &operator=(PthreadOwner &&other)

        noexcept;

        [[nodiscard]] int start(void *(*entry)(void *), void *argument)

        noexcept;

        [[nodiscard]] int join(void **result = nullptr)

        noexcept;

        [[nodiscard]] bool joinable() const

        noexcept {
            return started_ && !joined_;
        }

        [[nodiscard]] pthread_t native_handle() const

        noexcept {
            return thread_;
        }

    private:
        pthread_t thread_{};
        bool started_ = false;
        bool joined_ = false;
    };

    class ChildProcess final {
    public:
        constexpr ChildProcess()

        noexcept =
        default;

        explicit constexpr ChildProcess(pid_t pid)

        noexcept : pid_(pid) {}

        ~ChildProcess()

        noexcept;

        ChildProcess(const ChildProcess &) = delete;

        ChildProcess &operator=(const ChildProcess &) = delete;

        ChildProcess(ChildProcess &&other)

        noexcept;

        ChildProcess &operator=(ChildProcess &&other)

        noexcept;

        [[nodiscard]] bool valid() const

        noexcept {
            return pid_ > 0;
        }

        [[nodiscard]] pid_t get() const

        noexcept {
            return pid_;
        }

        [[nodiscard]] pid_t release()

        noexcept;

        [[nodiscard]] int terminate_and_wait(int signal_number)

        noexcept;

    private:
        pid_t pid_ = -1;
    };

}  // namespace ghostlock

#endif
