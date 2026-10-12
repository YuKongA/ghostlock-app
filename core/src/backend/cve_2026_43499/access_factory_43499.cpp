/** @file backend/cve_2026_43499/access_factory_43499.cpp
 *  Choosing the route's subclass. Unset and unknown routes are refused, not defaulted.
 */
#include "backend/cve_2026_43499/access_factory_43499.hpp"

#include "backend/cve_2026_43499/access_multicast_waiter.hpp"
#include "backend/cve_2026_43499/access_select_stack.hpp"
#include "backend/cve_2026_43499/access_tcp_zerocopy.hpp"

namespace ghostlock::backend::cve_2026_43499 {
    std::unique_ptr<contract::KernelAccess> create_kernel_access_43499(
        profile::RouteId route) noexcept {
        switch (route) {
            /* TODO(routeexec-compile-time): 机械改动 —— 路由子类成为模板后，本构造点必须点名它的
             *  路由类型。厂里出来的这一件**尚未接线**（无名 access：状态机可用、写被具名拒绝）⇒ 它的
             *  路由类型写作 `NoRouteExec`（＝「还没有路由」本身，一型各表；不是空实现塞给模板）。
             *  完成 = 三线门禁绿 ＋ 真机判据不变 ＋ 用户确认 ⇒ 删本标注。 */
            case profile::RouteId::MulticastWaiter:
                return std::make_unique<MulticastWaiterAccess<NoRouteExec>>();
            case profile::RouteId::SelectStack:
                return std::make_unique<SelectStackAccess>();
            case profile::RouteId::TcpZerocopy:
                return std::make_unique<TcpZerocopyAccess>();
            case profile::RouteId::Unset:
                break;   /* not decided is not a route: refused rather than defaulted */
        }
        return nullptr;
    }
}  // namespace ghostlock::backend::cve_2026_43499
