#include "tests/cpp_link_probe.h"
#include "target_constants.hpp"

#include <cerrno>
#include <cstddef>
#include <string>
#include <vector>

using namespace ghostlock;

namespace {
    class ErrnoRestore final {
    public:
        explicit ErrnoRestore(int value)

            noexcept : value_(value) {
        }

        ~ErrnoRestore()

            noexcept {
            errno = value_;
        }

        ErrnoRestore(const ErrnoRestore &) = delete;

        ErrnoRestore &operator=(const ErrnoRestore &) = delete;

    private:
        int value_;
    };
} // namespace

extern "C" __attribute__ ((used
,
visibility (
"default"
)
)
)
int ghostlock_cpp_link_probe(const char *text, int saved_errno) noexcept {
    try {
        ErrnoRestore restore(saved_errno);
        const std::string owned = text ? text : "";
        const std::vector<char> copied(owned.begin(), owned.end());
        const target::KernelImageAddress image(
            target::address::kImageTextBase);
        if (!image.checked_add(target::payload::kLockOffset)) {
            return -1;
        }
        errno = EIO;
        return static_cast<int>(owned.size() + copied.size()) + saved_errno;
    } catch (...) {
        errno = saved_errno;
        return -1;
    }
}
