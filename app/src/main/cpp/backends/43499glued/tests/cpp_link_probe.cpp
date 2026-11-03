#include "tests/cpp_link_probe.h"
#include "kernel/target_constants.hpp"

#include <cerrno>
#include <cstddef>
#include <string>
#include <vector>

using namespace ghostlock;

namespace {
    class ErrnoRestore final {
    public:
        explicit ErrnoRestore(int32_t value)

            noexcept : value_(value) {
        }

        ~ErrnoRestore()

            noexcept {
            errno = value_;
        }

        ErrnoRestore(const ErrnoRestore &) = delete;

        ErrnoRestore &operator=(const ErrnoRestore &) = delete;

    private:
        int32_t value_;
    };
} // namespace

extern "C" __attribute__ ((used
,
visibility (


"default"
)
)
)
int32_t ghostlock_cpp_link_probe(const char *text, int32_t saved_errno) noexcept {
    try {
        ErrnoRestore restore(saved_errno);
        const std::string owned = text ? text : "";
        const std::vector<char> copied(owned.begin(), owned.end());
        const target::KernelAddress<target::ImageAddressDomain> image(
            target::address::kImageTextBase);
        if (!image.checked_add(target::payload::kLockOffset)) {
            return -1;
        }
        errno = EIO;
        return static_cast<int32_t>(owned.size() + copied.size()) + saved_errno;
    } catch (...) {
        errno = saved_errno;
        return -1;
    }
}
