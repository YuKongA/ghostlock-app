/** @file jni/api_43499_device.cpp
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
#include "jni/api_43499_device.hpp"

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

    /** @return 0 成功；否则 Access 状态码（上层翻具名）。 */
    int device_initialize_43499(const std::uint64_t accessId, const std::uint64_t route,
                                const std::uint64_t offset, const bool offset_set) noexcept {
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
                           const std::vector<std::uint8_t> &block, const char **why) noexcept {
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
        const auto out = g_accesses_43499[slot]->write(
            address,
            std::span<const std::byte>(reinterpret_cast<const std::byte *>(block.data()), block.size()),
            ghostlock::contract::AccessChannel::PiWindow, params);
        if (!out.ok() && why != nullptr) {
            *why = ghostlock::contract::KernelAccess::status_name(out.status);
        }
        return out.ok();
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
