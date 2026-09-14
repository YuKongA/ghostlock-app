#include "support/native_resource.hpp"

#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <utility>

namespace ghostlock {

    UniqueFd::~UniqueFd()

    noexcept {
    reset();
}

UniqueFd::UniqueFd(UniqueFd &&other)

noexcept :
fd_(other
.

release()

) {
}

UniqueFd &UniqueFd::operator=(UniqueFd &&other)

noexcept {
if (this != &other)
reset(other
.

release()

);
return *this;
}

int UniqueFd::release()

noexcept {
return
std::exchange(fd_,
-1); }

void UniqueFd::reset(int fd)

noexcept {
const int old = std::exchange(fd_, fd);
if (old >= 0) {
const int saved_errno = errno;
close(old);
errno = saved_errno;
}
}

MappedRegion::~MappedRegion()

noexcept {
reset();

}

MappedRegion::MappedRegion(MappedRegion &&other)

noexcept
:
address_(other
.

release()

),
size_(std::exchange(other.size_, 0)
) {
}

MappedRegion &MappedRegion::operator=(MappedRegion &&other)

noexcept {
if (this != &other) {
reset();

size_ = other.size_;
address_ = other.release();
other.
size_ = 0;
}
return *this;
}

Result <MappedRegion> MappedRegion::map_anonymous(std::size_t size,
        int protection) {
    void *address = mmap(nullptr, size, protection, MAP_PRIVATE | MAP_ANONYMOUS,
            -1, 0);
    if (address == MAP_FAILED) {
        return Result<MappedRegion>::failure(SysError::from_errno());
    }
    return Result<MappedRegion>::success(MappedRegion(address, size));
}

bool MappedRegion::valid() const

noexcept {
return address_ !=
nullptr &&address_
!=
MAP_FAILED &&size_
!= 0;
}

std::span <std::byte> MappedRegion::bytes() const

noexcept {
return

valid()

? std::span(static_cast<std::byte *>(address_), size_)
:

std::span<std::byte>();

}

void *MappedRegion::release()

noexcept {
return
std::exchange(address_, nullptr
);
}

void MappedRegion::reset()

noexcept {
void *old = std::exchange(address_, nullptr);
const std::size_t old_size = std::exchange(size_, 0);
if (
old &&old
!=
MAP_FAILED &&old_size
) {
const int saved_errno = errno;
munmap(old, old_size
);
errno = saved_errno;
}
}

PthreadOwner::~PthreadOwner()

noexcept {
if (

joinable()

)
pthread_detach(thread_);
}

PthreadOwner::PthreadOwner(PthreadOwner &&other)

noexcept
:
thread_(other
.thread_),
started_(other
.started_),
joined_(other
.joined_) {
other.
started_ = false;
other.
joined_ = true;
}

PthreadOwner &PthreadOwner::operator=(PthreadOwner &&other)

noexcept {
if (this != &other) {
if (

joinable()

)
pthread_detach(thread_);
thread_ = other.thread_;
started_ = other.started_;
joined_ = other.joined_;
other.
started_ = false;
other.
joined_ = true;
}
return *this;
}

int PthreadOwner::start(void *(*entry)(void *), void *argument)

noexcept {
if (

joinable()

|| !entry) return
EINVAL;
const int error = pthread_create(&thread_, nullptr, entry, argument);
if (!error) {
started_ = true;
joined_ = false;
}
return
error;
}

int PthreadOwner::join(void **result)

noexcept {
if (!

joinable()

) return
EINVAL;
const int error = pthread_join(thread_, result);
if (!error)
joined_ = true;
return
error;
}

ChildProcess::~ChildProcess()

noexcept {
if (

valid()

) (void)
terminate_and_wait(SIGKILL);
}

ChildProcess::ChildProcess(ChildProcess &&other)

noexcept
:
pid_(other
.

release()

) {
}

ChildProcess &ChildProcess::operator=(ChildProcess &&other)

noexcept {
if (this != &other) {
if (

valid()

) (void)
terminate_and_wait(SIGKILL);
pid_ = other.release();
}
return *this;
}

pid_t ChildProcess::release()

noexcept {
return
std::exchange(pid_,
-1); }

int ChildProcess::terminate_and_wait(int signal_number)

noexcept {
if (!

valid()

) return
EINVAL;
const pid_t owned = release();
if (
kill(owned, signal_number
) != 0 && errno != ESRCH) return
errno;
while (
waitpid(owned, nullptr,
0) < 0) {
if (errno != EINTR) return
errno;
}
return 0;
}

}  // namespace ghostlock
