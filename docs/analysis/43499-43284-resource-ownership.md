# 43499 / 43284 资源所有权证据（附件 B）

> 计划：[43499-43284-cpp-parameterization-plan.md](../plan/43499-43284-cpp-parameterization-plan.md)。快照 HEAD `d2778a1a`；行号对应当前 `app/src/main/cpp`。本表是 D4 的证据；实现时逐行核对，不以本表替代代码审查。

## 43499

| 对象 | 创建 | owner | 借用 | 终结 | 释放/证据 |
|---|---|---|---|---|---|
| Glk43499Session | create | Kotlin/JNI | 全部线程/route | destroy | create 方；destroy 幂等 |
| futex 字 `f_wait/f_pi_target/f_pi_chain` | create/reset | session | waiter/owner | route_done + join | main.c:391-404 |
| 原子 `waiter_*/owner_*/consumer_*/route_done/punch_*` | create/reset | session | 三线程 | join | main.c:266-278,391-404 |
| waiter_thread | main.c:410 | session | futex words | join main.c:430 | pthread_join |
| owner_thread | main.c:411 | session | futex words | join main.c:431 | pthread_join |
| consumer_thread | main.c:412 | session | futex words | join main.c:432 | pthread_join |
| mr_tx / mr_ty | fops.c:90-91 | session | mr_* | join fops.c:112 | kernel5_resident_stop |
| tcp_punch_thread | fops.c:310 | route 栈 | tcp_punch_state | join fops.c:399-401 | do_tcp 的 out 路径 |
| `mr_fd` | fops.c:52 | session | mr_y | close fops.c:68 | kernel5_resident_stop / 线程退出 |
| `punch_fd` | fops.c:281 | route 栈 | tcp_punch_thread | close fops.c:405 | do_tcp 的 out 路径 |
| `map`（punch mmap） | fops.c:289 | route 栈 | tcp_punch_thread | munmap fops.c:402 | do_tcp 的 out 路径 |
| `listener` | fops.c:172 | tcp_make_pair | — | close fops.c:218 | 成功/失败均关 |
| `client_fd` | fops.c:201 | route 栈 | getsockopt | close fops.c:411 | 失败路径 fops.c:209/220 |
| `server_fd` | fops.c:216 | route 栈 | send | close fops.c:408 | 失败路径 fops.c:220 |
| `pipefd[2]` | fops.c:603 | do_pselect | select | close fops.c:722-723；exception: process exit | consumer_stuck 时故意泄漏（fops.c:712-717），D4 例外 `leak-on-stuck`，须用户批准记录明示接受 |
| `block_fd`（timerfd） | fops.c:609 | do_pselect | select | close fops.c:719-721；exception: process exit | 与 pipefd[0] 同一 fd 时不重复关；consumer_stuck 时同 `leak-on-stuck`，不 destroy close |
| `high_read` | fops.c:615 | do_pselect | open_selected_fds | close fops.c:649 | 失败路径 fops.c:620-625 |
| `standard_io_backup[3]` | fops.c:509 | route 栈 | select | restore fops.c:521 | 同函数返回前 |
| `perf_fd` | main.c:792 | session | perf sample | close main.c:834 | 失败路径 main.c:801 |
| perf `buf` mmap | main.c:798 | session | sample | munmap main.c:834 | 失败路径 main.c:800 |
| `reclaim_sv[2]` | util.c:706 | session | sendmsg | close util.c:293-300 | close_reclaim_sockets |
| `quarantined_reclaim_sv[2]` | util.c:302 | session | W1 repair | close util.c:313-320 | release_quarantined |
| `prebuilt_reclaim_sv[2]` | util.c:322 | session | W2 fast repair | close util.c:357-364 | discard_prebuilt |
| `pcp_shaping_sv[2]` | util.c:714 | prepare_kernel_page 栈 | sendmsg | close util.c:746-747 | 同函数 |
| ctx memfds（prepare/spray/pre/post） | util.c:576-605 | session | spray | close util.c:366-373 | cleanup_page_prepare_state |
| `memfd_leak` | util.c:603 | session | leak child | close util.c:752 / 388-391 | cleanup |
| child pids（prepare/spray/pre/post） | util.c:576-605 | session | spray | kill/wait util.c:285-291 | cleanup |
| KernelSnitch `ks` | util.c:587 | session | leak child | cleanup util.c:762 | 失败路径 util.c:663/686/698 |
| `skb_buf` | util.c:573 | session | sendmsg | free util.c:396 | cleanup |
| `page_base/fake_*` | util.c:435-438 | session | route | discard/cleanup | prebuilt util.c:322-364 |
| root script | main.c:602 | 当前 C；P2 迁 Kotlin | child | Kotlin 删除/替换 | 当前 main.c:604-756 |
| SIGUSR1 handler | fops.c:89-90 | session | mr_y | 恢复/进程退出 | mr_intr fops.c:24 |
| SIGPIPE | main.c:1137 | 进程 | — | 进程退出 | SIG_IGN |

## 43284

| 对象 | 创建 | owner | 借用 | 终结 | 释放/证据 |
|---|---|---|---|---|---|
| `g_libcxx_r` 原字节 | exp.c:448-452 | 43284 session | cleanup | cleanup 恢复 | exp.c:393-403 |
| `sk_send` | exp.c:166 | patch_file_cbc | do_one_write_cbc | close exp.c:234 | 失败路径 exp.c:192/200/234 |
| `file_fd` | exp.c:189 | patch_file_cbc | splice | close exp.c:233 | 失败路径 exp.c:199 |
| `pfd[2]` | exp.c:197 | patch_file_cbc | do_one_write_cbc | close exp.c:232 | 失败路径 exp.c:199-201 |
| `rdpipe[2]` | exp.c:82 | read_vendor_content | read | close exp.c:86-90 | 函数返回前 |
| helper/crash_dump child | exp.c:64 | spawn_crash_dump | pipe | waitpid exp.c:75 | 函数返回前 |
| orphan child | exp.c:473-479 | createOrphanProcess | — | 故意孤儿 | 双 fork；父 waitpid |
| `libcxx_ko_target` | exp.c:541 | session | patch_ko | 进程退出 | 指向 libcxx_data 内 64B |
| `g_seq` | exp.c:30 | Runtime | do_one_write_cbc | 进程退出 | 每次写递增 |

## 顺序检查

- 43499 main route：waiter 停止访问（fops route 返回）→ `route_done`（main.c:320）→ unlock chain（main.c:321）→ join（main.c:430-432）→ 关闭 reclaim/socket（util.c:293）→ 释放页（util.c:383-398）。
- 5.x resident：`mr_stop`（fops.c:111）→ join mr_ty/mr_tx（fops.c:112）→ close_reclaim/cleanup（fops.c:113）。
- tcp：consumer stop（fops.c:396）→ punch_stop（fops.c:398）→ join puncher（fops.c:400）→ munmap（fops.c:403）→ close punch/server/client（fops.c:405-413）。
- pselect：consumer_inflight 归零或判定 stuck（fops.c:690-696）→ 关闭 block/pipe（fops.c:718-724）；stuck 时故意泄漏并在报告标 `route_last_step=34`。
- 43284：patch_file_cbc 的 fd 在循环结束后统一关（exp.c:232-234）；patch_hook 先保存原字节再写（exp.c:443-455），cleanup 逆序恢复 trampoline/shellcode（exp.c:397-402）。

> 任何“提前 close/unmap”或“join 前释放”的实现改动 = 阻断；必须回到本表逐行核对。
