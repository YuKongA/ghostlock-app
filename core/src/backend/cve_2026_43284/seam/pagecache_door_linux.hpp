#pragma once
/** @file backend/cve_2026_43284/seam/pagecache_door_linux.hpp
 *  The two file descriptors the page cache write needs, on a real device.
 *
 *  DoorOps is the interface; this is the implementation that talks to the kernel and the network. It
 *  is the piece the retired tree kept beside its execution binding, and it is deliberately the only
 *  place in this backend that opens something or connects a socket.
 *
 *  Two things it does not do. It does not decide whether the target may be used: the carrier path
 *  comes from the profile, which is matched against the running kernel, and the binding checks for a
 *  development path where it belongs. And it does not repair a failure: every call answers with a
 *  negative descriptor or a closed one, and the caller names the step.
 *
 *  The path is bounded at construction, because a profile is text of a declared maximum width and a
 *  path copied into a fixed buffer must not be able to overrun it.
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43284/seam/pagecache_doors.hpp"

namespace ghostlock::backend::cve_2026_43284::seam {
    /** How many bytes a carrier path may occupy, including its terminator. */
    inline constexpr std::size_t kDoorPathBytes = 256U;

    /**
     *  The real doors: the target opened read-only, and a UDP socket connected to the peer the
     *  session material names.
     *
     *  @note  The carrier path is copied in at construction and truncated rather than allowed to
     *         overrun; a path that does not fit is refused by the constructor instead, so an
     *         unusable door cannot be built by accident.
     */
    class PageCacheDoorLinux final : public DoorOps {
      public:
        /**
         *  @param carrier_path The target to open for reading, from the profile's carrier.
         *  @param path_size    Its length. A path of zero bytes or one that does not fit is refused.
         */
        PageCacheDoorLinux(const char *carrier_path, std::size_t path_size) noexcept;

        [[nodiscard]] bool usable() const noexcept { return usable_; }

        /** Open the carrier read-only. A negative value on any failure. */
        [[nodiscard]] int open_target() noexcept override;

        /** A UDP socket connected to the session's peer. A negative value on any failure. */
        [[nodiscard]] int connect_esp(const SaMaterial &sa) noexcept override;

        /** Close a descriptor this door opened. */
        void close_fd(int fd) noexcept override;

      private:
        char path_[kDoorPathBytes] = {};
        bool usable_ = false;
    };
}  // namespace ghostlock::backend::cve_2026_43284::seam
