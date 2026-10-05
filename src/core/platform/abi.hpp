#ifndef GHOSTLOCK_PLATFORM_ABI_HPP
#define GHOSTLOCK_PLATFORM_ABI_HPP

/* platform::abi owner schema/view (ADR-0003 / ADR-0004 R5, A2-4-3).
 *
 * Platform ABI: the task_struct layout offsets, the struct cred *layout* offsets,
 * the kernel symbol offsets and the device phys facts. These keys used to be
 * declared inside the cve_2026_43499 backend Schema; they now have their own
 * owner (ADR-0003 decision 5: the interpreting layer owns the field). The
 * backend keeps only the values it interprets (the credential template values,
 * the KASLR slide targets, the route/spray geometry and the step set).
 *
 * Physical freeze: A2-4-3 does NOT move kernel_offsets. The platform View is a
 * typed, standalone struct; composition binds it and then mechanically merges it
 * into the frozen transport (apply_to()), which is the one cross-owner mapping
 * ADR-0003 tolerates while memory/attack code keeps reading kernel_offsets by
 * offset (plan section 3.3 / R-3). apply_to is exercised by platform_abi_test.
 *
 * The runtime offset accessors that read the active session are the neutral
 * profile/runtime_struct_offsets.h surface, bound to the backend by
 * backend/cve_2026_43499/backend_offsets.cpp; this header declares only the
 * owner Schema/View and the mechanical merge. */

#include "profile/glkv3.hpp"
#include "contract/model.hpp"
#include "profile/schema.hpp"

#include <cstdint>
#include <optional>
#include <type_traits>

namespace ghostlock::platform::abi {
    /* Platform half of the frozen transport. Field names mirror
     * kernel_offsets so the merge below is a plain member copy. */
    struct TaskLayout final {
        uint32_t prio = 0, normal_prio = 0, sched_task_group = 0;
        uint32_t pi_lock = 0, pi_waiters = 0, pi_top_task = 0, pi_blocked_on = 0;
        uint32_t pid = 0, tgid = 0, atomic_flags = 0;
        uint32_t real_cred = 0, cred = 0, comm = 0, tasks = 0, seccomp = 0;
    };

    struct CredLayout final {
        uint32_t usage_offset = 0, caps_offset = 0, ref_count = 0;
        uint32_t ref0_offset = 0, ref1_offset = 0, ref2_offset = 0, ref3_offset = 0;
    };

    struct SymbolLayout final {
        uint64_t init_task = 0, init_cred = 0, empty_zero_page = 0;
        uint64_t root_task_group = 0, selinux_enforcing = 0;
        uint64_t selinux_blob_sizes = 0, security_hook_heads = 0;
    };

    struct PhysLayout final {
        std::optional<uint64_t> kernel_phys_load;
        std::optional<uint64_t> kernel_phys_offset;
    };

    struct View final {
        TaskLayout task{};
        CredLayout cred{};
        SymbolLayout offset{};
        PhysLayout kernel{};
    };

    namespace detail {
        template<typename T>
        [[nodiscard]] constexpr T from_raw(uint64_t raw) noexcept {
            if constexpr (std::is_signed_v<T>) {
                return static_cast<T>(static_cast<int64_t>(raw));
            } else {
                return static_cast<T>(raw);
            }
        }
    } // namespace detail

    using AbiField = profile::FieldSpec<View>;

    /* Owner declaration: exactly the platform keys, one spec each. The
     * destination widths mirror the removed 43499 declarations one for one. */
    struct Schema final {
        using View = abi::View;

        static constexpr AbiField kFields[] = {
            {"platform.abi.task_struct", "prio", 4, false, false, [](View &view, uint64_t raw) { view.task.prio = detail::from_raw<decltype(view.task.prio)>(raw); }},
            {"platform.abi.task_struct", "normal_prio", 4, false, false, [](View &view, uint64_t raw) { view.task.normal_prio = detail::from_raw<decltype(view.task.normal_prio)>(raw); }},
            {"platform.abi.task_struct", "sched_task_group", 4, false, false, [](View &view, uint64_t raw) { view.task.sched_task_group = detail::from_raw<decltype(view.task.sched_task_group)>(raw); }},
            {"platform.abi.task_struct", "pi_lock", 4, false, false, [](View &view, uint64_t raw) { view.task.pi_lock = detail::from_raw<decltype(view.task.pi_lock)>(raw); }},
            {"platform.abi.task_struct", "pi_waiters", 4, false, false, [](View &view, uint64_t raw) { view.task.pi_waiters = detail::from_raw<decltype(view.task.pi_waiters)>(raw); }},
            {"platform.abi.task_struct", "pi_top_task", 4, false, false, [](View &view, uint64_t raw) { view.task.pi_top_task = detail::from_raw<decltype(view.task.pi_top_task)>(raw); }},
            {"platform.abi.task_struct", "pi_blocked_on", 4, false, false, [](View &view, uint64_t raw) { view.task.pi_blocked_on = detail::from_raw<decltype(view.task.pi_blocked_on)>(raw); }},
            {"platform.abi.task_struct", "pid", 4, false, false, [](View &view, uint64_t raw) { view.task.pid = detail::from_raw<decltype(view.task.pid)>(raw); }},
            {"platform.abi.task_struct", "tgid", 4, false, false, [](View &view, uint64_t raw) { view.task.tgid = detail::from_raw<decltype(view.task.tgid)>(raw); }},
            {"platform.abi.task_struct", "atomic_flags", 4, false, false, [](View &view, uint64_t raw) { view.task.atomic_flags = detail::from_raw<decltype(view.task.atomic_flags)>(raw); }},
            {"platform.abi.task_struct", "real_cred", 4, false, false, [](View &view, uint64_t raw) { view.task.real_cred = detail::from_raw<decltype(view.task.real_cred)>(raw); }},
            {"platform.abi.task_struct", "cred", 4, false, false, [](View &view, uint64_t raw) { view.task.cred = detail::from_raw<decltype(view.task.cred)>(raw); }},
            {"platform.abi.task_struct", "comm", 4, false, false, [](View &view, uint64_t raw) { view.task.comm = detail::from_raw<decltype(view.task.comm)>(raw); }},
            {"platform.abi.task_struct", "tasks", 4, false, false, [](View &view, uint64_t raw) { view.task.tasks = detail::from_raw<decltype(view.task.tasks)>(raw); }},
            {"platform.abi.task_struct", "seccomp", 4, false, false, [](View &view, uint64_t raw) { view.task.seccomp = detail::from_raw<decltype(view.task.seccomp)>(raw); }},
            {"platform.abi.cred", "usage_offset", 4, false, false, [](View &view, uint64_t raw) { view.cred.usage_offset = detail::from_raw<decltype(view.cred.usage_offset)>(raw); }},
            {"platform.abi.cred", "caps_offset", 4, false, false, [](View &view, uint64_t raw) { view.cred.caps_offset = detail::from_raw<decltype(view.cred.caps_offset)>(raw); }},
            {"platform.abi.cred", "ref_count", 4, false, false, [](View &view, uint64_t raw) { view.cred.ref_count = detail::from_raw<decltype(view.cred.ref_count)>(raw); }},
            {"platform.abi.cred", "ref0_offset", 4, false, false, [](View &view, uint64_t raw) { view.cred.ref0_offset = detail::from_raw<decltype(view.cred.ref0_offset)>(raw); }},
            {"platform.abi.cred", "ref1_offset", 4, false, false, [](View &view, uint64_t raw) { view.cred.ref1_offset = detail::from_raw<decltype(view.cred.ref1_offset)>(raw); }},
            {"platform.abi.cred", "ref2_offset", 4, false, false, [](View &view, uint64_t raw) { view.cred.ref2_offset = detail::from_raw<decltype(view.cred.ref2_offset)>(raw); }},
            {"platform.abi.cred", "ref3_offset", 4, false, false, [](View &view, uint64_t raw) { view.cred.ref3_offset = detail::from_raw<decltype(view.cred.ref3_offset)>(raw); }},
            {"platform.abi.offset", "init_task", 8, false, false, [](View &view, uint64_t raw) { view.offset.init_task = detail::from_raw<decltype(view.offset.init_task)>(raw); }},
            {"platform.abi.offset", "init_cred", 8, false, false, [](View &view, uint64_t raw) { view.offset.init_cred = detail::from_raw<decltype(view.offset.init_cred)>(raw); }},
            {"platform.abi.offset", "empty_zero_page", 8, false, false, [](View &view, uint64_t raw) { view.offset.empty_zero_page = detail::from_raw<decltype(view.offset.empty_zero_page)>(raw); }},
            {"platform.abi.offset", "root_task_group", 8, false, false, [](View &view, uint64_t raw) { view.offset.root_task_group = detail::from_raw<decltype(view.offset.root_task_group)>(raw); }},
            {"platform.abi.offset", "selinux_enforcing", 8, false, false, [](View &view, uint64_t raw) { view.offset.selinux_enforcing = detail::from_raw<decltype(view.offset.selinux_enforcing)>(raw); }},
            {"platform.abi.offset", "selinux_blob_sizes", 8, false, false, [](View &view, uint64_t raw) { view.offset.selinux_blob_sizes = detail::from_raw<decltype(view.offset.selinux_blob_sizes)>(raw); }},
            {"platform.abi.offset", "security_hook_heads", 8, false, false, [](View &view, uint64_t raw) { view.offset.security_hook_heads = detail::from_raw<decltype(view.offset.security_hook_heads)>(raw); }},
            {"platform.abi.kernel", "kernel_phys_load", 8, false, false, [](View &view, uint64_t raw) { view.kernel.kernel_phys_load = detail::from_raw<std::remove_reference_t<decltype(*view.kernel.kernel_phys_load)>>(raw); }},
            {"platform.abi.kernel", "kernel_phys_offset", 8, false, false, [](View &view, uint64_t raw) { view.kernel.kernel_phys_offset = detail::from_raw<std::remove_reference_t<decltype(*view.kernel.kernel_phys_offset)>>(raw); }},
        };
    };

    /* GLKv3 declaration of the same (section, key) set; the v2/v3 lockstep is
     * asserted by glkv3_schema_test. Kept optional (presence = key occurrence)
     * exactly like the v2 Schema. */
    inline constexpr profile::glkv3::FieldSpec kPlatformAbiGlkv3Fields[] = {
        {"platform.abi.task_struct", "prio", profile::glkv3::WireType::UInt, false},
        {"platform.abi.task_struct", "normal_prio", profile::glkv3::WireType::UInt, false},
        {"platform.abi.task_struct", "sched_task_group", profile::glkv3::WireType::UInt, false},
        {"platform.abi.task_struct", "pi_lock", profile::glkv3::WireType::UInt, false},
        {"platform.abi.task_struct", "pi_waiters", profile::glkv3::WireType::UInt, false},
        {"platform.abi.task_struct", "pi_top_task", profile::glkv3::WireType::UInt, false},
        {"platform.abi.task_struct", "pi_blocked_on", profile::glkv3::WireType::UInt, false},
        {"platform.abi.task_struct", "pid", profile::glkv3::WireType::UInt, false},
        {"platform.abi.task_struct", "tgid", profile::glkv3::WireType::UInt, false},
        {"platform.abi.task_struct", "atomic_flags", profile::glkv3::WireType::UInt, false},
        {"platform.abi.task_struct", "real_cred", profile::glkv3::WireType::UInt, false},
        {"platform.abi.task_struct", "cred", profile::glkv3::WireType::UInt, false},
        {"platform.abi.task_struct", "comm", profile::glkv3::WireType::UInt, false},
        {"platform.abi.task_struct", "tasks", profile::glkv3::WireType::UInt, false},
        {"platform.abi.task_struct", "seccomp", profile::glkv3::WireType::UInt, false},
        {"platform.abi.cred", "usage_offset", profile::glkv3::WireType::UInt, false},
        {"platform.abi.cred", "caps_offset", profile::glkv3::WireType::UInt, false},
        {"platform.abi.cred", "ref_count", profile::glkv3::WireType::UInt, false},
        {"platform.abi.cred", "ref0_offset", profile::glkv3::WireType::UInt, false},
        {"platform.abi.cred", "ref1_offset", profile::glkv3::WireType::UInt, false},
        {"platform.abi.cred", "ref2_offset", profile::glkv3::WireType::UInt, false},
        {"platform.abi.cred", "ref3_offset", profile::glkv3::WireType::UInt, false},
        {"platform.abi.offset", "init_task", profile::glkv3::WireType::UInt, false},
        {"platform.abi.offset", "init_cred", profile::glkv3::WireType::UInt, false},
        {"platform.abi.offset", "empty_zero_page", profile::glkv3::WireType::UInt, false},
        {"platform.abi.offset", "root_task_group", profile::glkv3::WireType::UInt, false},
        {"platform.abi.offset", "selinux_enforcing", profile::glkv3::WireType::UInt, false},
        {"platform.abi.offset", "selinux_blob_sizes", profile::glkv3::WireType::UInt, false},
        {"platform.abi.offset", "security_hook_heads", profile::glkv3::WireType::UInt, false},
        {"platform.abi.kernel", "kernel_phys_load", profile::glkv3::WireType::UInt, false},
        {"platform.abi.kernel", "kernel_phys_offset", profile::glkv3::WireType::UInt, false},
    };


    /* Mechanical platform-View -> frozen-transport merge (ADR-0003 R-3). The
     * one allowed cross-owner mapping; platform_abi_test pins every field. */
    inline void apply_to(const View &view, profile::kernel_offsets &out) noexcept {
        out.task.prio = view.task.prio;
        out.task.normal_prio = view.task.normal_prio;
        out.task.sched_task_group = view.task.sched_task_group;
        out.task.pi_lock = view.task.pi_lock;
        out.task.pi_waiters = view.task.pi_waiters;
        out.task.pi_top_task = view.task.pi_top_task;
        out.task.pi_blocked_on = view.task.pi_blocked_on;
        out.task.pid = view.task.pid;
        out.task.tgid = view.task.tgid;
        out.task.atomic_flags = view.task.atomic_flags;
        out.task.real_cred = view.task.real_cred;
        out.task.cred = view.task.cred;
        out.task.comm = view.task.comm;
        out.task.tasks = view.task.tasks;
        out.task.seccomp = view.task.seccomp;
        out.credential.usage_offset = view.cred.usage_offset;
        out.credential.caps_offset = view.cred.caps_offset;
        out.credential.ref_count = view.cred.ref_count;
        out.credential.ref0_offset = view.cred.ref0_offset;
        out.credential.ref1_offset = view.cred.ref1_offset;
        out.credential.ref2_offset = view.cred.ref2_offset;
        out.credential.ref3_offset = view.cred.ref3_offset;
        out.offsets.init_task = view.offset.init_task;
        out.offsets.init_cred = view.offset.init_cred;
        out.offsets.empty_zero_page = view.offset.empty_zero_page;
        out.offsets.root_task_group = view.offset.root_task_group;
        out.offsets.selinux_enforcing = view.offset.selinux_enforcing;
        out.offsets.selinux_blob_sizes = view.offset.selinux_blob_sizes;
        out.offsets.security_hook_heads = view.offset.security_hook_heads;
        out.misc.kernel_phys_load = view.kernel.kernel_phys_load;
        out.misc.kernel_phys_offset = view.kernel.kernel_phys_offset;
    }
} // namespace ghostlock::platform::abi

#endif
