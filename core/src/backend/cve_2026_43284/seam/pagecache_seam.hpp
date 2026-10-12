#pragma once
/** @file backend/cve_2026_43284/seam/pagecache_seam.hpp
 *  The second backend behind the access seam: the page cache write, and only it.
 *
 *  Shape mirrors the first backend's seam because the interface is the same one: the access decides
 *  and the seam acts, the access brings the route up in initialize and takes it down in shutdown,
 *  and construction does nothing. What differs is what this route is, and it is worth naming: its
 *  write is not a word through a race window but a sixteen-byte block through the page cache, so
 *  the width it declares is sixteen and the window interface below takes and returns sixteen-byte
 *  blocks rather than a word.
 *
 *  The doors and the secrets are separated by where they can come from. The two descriptors and
 *  the splice face are opened and prepared here, because the access owns its route and the ruling
 *  is that it prepares what it uses; the key material cannot be opened by anyone here -- it is a
 *  runtime credential that arrives with the parameters -- so it is received and held by value so it
 *  can be zeroed on the way out.
 *
 *  Scope, stated rather than implied: one route, both faces. The block read was not optional even
 *  before it was public -- the old write derives its IV from the block it is about to replace -- and
 *  the same read is what a patch is verified against, so the read face is served from the same door
 *  and the same surface rather than from a second path.
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43284/access_params.hpp"
#include "backend/cve_2026_43499/kernel_access.hpp"

namespace ghostlock::backend::cve_2026_43284::seam {
    /** How wide this backend's write is. Sixteen bytes, because that is the page cache block the
     *  primitive writes; the first backend's window writes a word, and the interface asks rather
     *  than assumes. */
    inline constexpr std::size_t kWriteWidth = 16U;

    /** The window, as this seam sees it: the page cache write. The old body implements it; until
     *  then a recording one drives the tests. */
    class PageCacheWindow {
    public:
        PageCacheWindow() noexcept = default;
        PageCacheWindow(const PageCacheWindow &) = delete;
        PageCacheWindow &operator=(const PageCacheWindow &) = delete;
        virtual ~PageCacheWindow() noexcept = default;

        /** Open the doors the write needs: the file descriptor and the ESP socket. Called once
         *  per route lifetime, from bring_up. */
        [[nodiscard]] virtual bool open_doors(const SaMaterial &sa) noexcept = 0;
        /** Close them again. Called from tear_down. */
        virtual void close_doors() noexcept = 0;
        /** Write one sixteen-byte block at a position. */
        [[nodiscard]] virtual bool write_block(std::uint64_t position,
                                               const std::uint8_t block[kWriteWidth]) noexcept = 0;
        /** Read one sixteen-byte block at a position. The answer is the carrier's own content at
         *  that position, which is what a patch is verified against -- the old tree read the block
         *  back after writing it and compared, so the read is part of the write's own check rather
         *  than a diagnostic. A window with no read face refuses instead of filling the buffer with
         *  zeros, because zeros are a plausible block and would read as data.
         *  @param position Where in the carrier the block starts.
         *  @param out      Receives the block. Left untouched when the answer is false.
         *  @return True only when a whole block was read. */
        [[nodiscard]] virtual bool read_block(std::uint64_t position,
                                              std::uint8_t out[kWriteWidth]) noexcept {
            static_cast<void>(position);
            static_cast<void>(out);
            return false;
        }
        /** Whether this route can also read. The real window answers yes while its carrier door is
         *  open; a recording window keeps the default, which is why the default exists. */
        [[nodiscard]] virtual bool can_read() const noexcept { return false; }
        /** Continue the session's sequence from where an earlier window left it. The sequence belongs
         *  to the security association and the kernel's inbound anti-replay window drops anything it
         *  has seen, so a window that restarted at one writes nothing at all. A window that owns no
         *  sequence ignores this, which is why the default does nothing. */
        virtual void adopt_sequence(std::uint32_t) noexcept {}
    };

    /** The page cache route, behind the seam. */
    class PageCacheSeam final : public cve_2026_43499::AccessSeam {
    public:
        /** The window is borrowed and must outlive this seam. */
        explicit PageCacheSeam(PageCacheWindow &window) noexcept : window_(window) {}

        /** This backend has an empty route axis, so the route id is not consulted; what it needs
         *  is credentials, and without them it does not come up. */
        [[nodiscard]] bool bring_up(profile::RouteId, const cve_2026_43499::TargetState &) noexcept override {
            return false;
        }

        /** Bring the route up with the credentials the caller holds. Separate from the seam's
         *  bring_up because the base signature has no place for them, which is the same reason the
         *  parameter set exists. */
        [[nodiscard]] bool bring_up_with(const SaMaterial &sa) noexcept {
            if (up_ || !sa.set) {
                return false;
            }
            if (!window_.open_doors(sa)) {
                return false;
            }
            sa_ = sa;
            up_ = true;
            return true;
        }

        [[nodiscard]] bool tear_down() noexcept override {
            if (!up_) {
                return false;
            }
            window_.close_doors();
            zeroize_secrets();
            up_ = false;
            return true;
        }

        /** One block. The value arrives as the block's bytes; the seam does not interpret it. */
        [[nodiscard]] bool land_block(std::uint64_t position,
                                      const std::uint8_t block[kWriteWidth]) noexcept {
            if (!up_) {
                return false;
            }
            return window_.write_block(position, block);
        }

        /** Continue the session's sequence. The access calls this after bring-up when the caller
         *  supplied one: bring-up starts a fresh route and a fresh sequence, so the adoption has to
         *  come after it -- the device proved that ordering twice, by dropping every datagram of a
         *  window that restarted at one. */
        void adopt_sequence(std::uint32_t next) noexcept { window_.adopt_sequence(next); }

        /** One block read back. It travels the door the write opened, so a seam that is down has no
         *  door and refuses before the window is asked; a window that cannot read is asked anyway,
         *  because its own answer is the honest one. */
        [[nodiscard]] bool read_block(std::uint64_t position,
                                      std::uint8_t out[kWriteWidth]) noexcept {
            if (!up_) {
                return false;
            }
            if (!window_.can_read()) {
                return false;
            }
            return window_.read_block(position, out);
        }

        /** The seam base offers a word slot, and this route has no word: its unit is a
         *  sixteen-byte block. So it refuses here rather than truncating or padding, and the
         *  refusal a caller actually sees comes from the access asking the width first.
         *  Implementing it as a refusal is not a stub: it is the true answer to a question
         *  this backend cannot serve. */
        [[nodiscard]] bool land_word(std::uint64_t address, std::uint64_t value,
                                     std::uint64_t offset) noexcept override {
            static_cast<void>(address);
            static_cast<void>(value);
            static_cast<void>(offset);
            return false;
        }
        [[nodiscard]] bool can_read() const noexcept override { return window_.can_read(); }

        [[nodiscard]] bool up() const noexcept { return up_; }
        /** Width, asked rather than assumed. */
        [[nodiscard]] static constexpr std::size_t width() noexcept { return kWriteWidth; }

    private:
        /** The credentials are held only while the route is up, and cleared when it goes down. */
        void zeroize_secrets() noexcept {
            for (std::size_t i = 0U; i < sizeof(sa_.aes_key); ++i) {
                sa_.aes_key[i] = 0U;
            }
            for (std::size_t i = 0U; i < sizeof(sa_.hmac_key); ++i) {
                sa_.hmac_key[i] = 0U;
            }
            sa_.spi = 0U;
            sa_.set = false;
        }

        PageCacheWindow &window_;
        SaMaterial sa_{};
        bool up_ = false;
    };
}  // namespace ghostlock::backend::cve_2026_43284::seam
