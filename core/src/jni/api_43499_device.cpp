/** @file jni/api_43499_device.cpp
 *  43499 的设备侧原语：只做「按 id 查表 + 调一个既有原语 + 回来」。
 *
 *  访问对象按 route 从工厂取（create_kernel_access_43499）—— route 取值来自档案，
 *  由 Kotlin 传进来；这里不做任何选择。
 */
#include <memory>

#include "backend/cve_2026_43499/access_factory_43499.hpp"
#include "jni/api_43499_device.hpp"

namespace ghostlock::jni {
    namespace {
        /** 43499 的内核访问表：按不透明 id 持有（不可拷贝，放 unique_ptr）。 */
        constexpr std::size_t kMaxAccesses43499 = 4U;
        std::unique_ptr<contract::KernelAccess> g_accesses_43499[kMaxAccesses43499];
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
}  // namespace ghostlock::jni
