# 43499 / 43284 核心清单与全局/字面量盘点（附件）

> 计划：[43499-43284-cpp-parameterization-plan.md](../plan/43499-43284-cpp-parameterization-plan.md)。快照 HEAD `d2778a1a`；基线 `libghostlock43499.so` `e0fc7fe5…f9c475`、`libghostlock43284.so` `5d922ffb…c8b9f4`。指令数用 `llvm-objdump -d` 实测；符号用 `llvm-nm` 可见（`readelf` 在本机不存在，不能作为 stripped 证据）。核验命令：`llvm-nm <so> | rg ' (T|t) (waiter_thread|patch_hook)$'`，实得 `T waiter_thread`、`T patch_hook`。

## A. 28 个 cmp 目标（一行一函数）

| id | C 符号 | 签名 | file:line | insn | 形态 |
|---|---|---|---|---:|---|
| 43499.01 | waiter_thread | `void *(void *)` | main.c:282 | 205 | C |
| 43499.02 | owner_thread | `void *(void *)` | main.c:326 | 84 | C |
| 43499.03 | consumer_thread | `void *(void *)` | main.c:340 | 244 | C |
| 43499.04 | run_main_route_threads | `int (void)` | main.c:406 | 254 | C |
| 43499.05 | do_kernel5_fake_lock_route | `void (void)` | fops.c:117 | 225 | C |
| 43499.06 | do_tcp_fake_lock_route | `void (void)` | fops.c:256 | 738 | C |
| 43499.07 | do_pselect_fake_lock_route | `void (void)` | fops.c:589 | 467 | C |
| 43499.08 | do_one_write | `int (uintptr_t,const char*,int,int)` | main.c:439 | 155 | C |
| 43499.09 | kernel5_resident_start | `int (void)` | fops.c:80 | 288 | C |
| 43499.10 | kernel5_resident_write | `int (uintptr_t,uintptr_t)` | fops.c:103 | 57 | C |
| 43499.11 | kernel5_resident_stop | `void (void)` | fops.c:110 | 35 | C |
| 43499.12 | prepare_skb_payload | `int (uintptr_t)` | util.c:425 | 741 | C |
| 43499.13 | prepare_kernel_page | `uintptr_t (void)` | util.c:562 | 1110 | C |
| 43499.14 | prepare_good_kernel_page | `uintptr_t (void)` | util.c:774 | 94 | C |
| 43499.15 | run_exploit | `int (int,char**)` | main.c:1133 | 1383 | C |
| 43284.01 | spawn_crash_dump | `int (off_t,int,int,const char*)` | exp.c:60 | 137 | C |
| 43284.02 | read_vendor_content | `int (off_t,uint8_t[16])` | exp.c:81 | 172 | C |
| 43284.03 | do_one_write_cbc | `int (int,int,int,int,off_t,const uint8_t[16],int)` | exp.c:121 | 154 | C |
| 43284.04 | patch_file_cbc | `int (const char*,const char*,size_t,size_t,int)` | exp.c:158 | 341 | C |
| 43284.05 | patch_helper | `int (void)` | exp.c:342 | 140 | C |
| 43284.06 | patch_ko | `int (void)` | exp.c:369 | 123 | C |
| 43284.07 | patch_hook | `int (const char*,const char*,char*,uint32_t,char*,char*,struct PatchRestore*)` | exp.c:409 | 372 | C |
| 43284.08 | find_hook_target | `int (const char*,const char*,uint64_t*,uint64_t*,uint32_t*,size_t)` | elf_parser.c:53 | 832 | C |
| 43284.09 | createOrphanProcess | `int (void)` | exp.c:470 | 71 | C |
| 43284.10 | exploit | `int (void)` | exp.c:552 | 162 | C |
| 43284.11 | setup | `int (int,char**)` | exp.c:512 | 254 | C |
| 43284.12 | cleanup | `void (void)` | exp.c:393 | 58 | C |
| 43284.13 | compute_iv | `void (const uint8_t[16],const uint8_t[16],uint8_t[16])` | exp.c:49 | 44 | C |

`libcxx_start`（`libcxx.S:33`）是汇编入口，单列；`tcp_punch_thread`（`fops.c:227`）、`mr_x/mr_y`（`fops.c:70/43`）是 route 内部线程，列入 B 但不计入 28 个 cmp 目标。

## B. 全局与所有权

### B.1 43499 可变全局（精确符号）

| 文件 | 符号 | 行 | 会话字段/去向 |
|---|---|---:|---|
| main.c | `active_offsets` | 19 | Target（create 读一次） |
| main.c | `g_home_dir`, `g_root_script_path` | 21-22 | Config（P2 迁 Kotlin） |
| main.c | `g_external_offsets`, `g_external_release` | 99-100 | 初始化临时，create 后释放 |
| main.c | `t0` | 248 | Runtime（timing） |
| main.c | `f_wait`, `f_pi_target`, `f_pi_chain` | 263-265 | Runtime futex words |
| main.c | `waiter_ready/waiting`, `owner_started/chain_done/stop`, `route_done`, `waiter_tid`, `punch_consume_go/stop`, `consumer_calls/success/inflight`, `main_route_delay_usec` | 266-278 | Runtime atomics |
| main.c | `fast_repair_route`, `memfd_leak` | 279-280 | Runtime |
| main.c | `g_core_main`, `g_core_consumer` | 538-539 | Config CPU |
| main.c | `run_exploit.vr_needed` | 1298 | Runtime（函数静态） |
| fops.c | `route_last_step/errno` | 12-13 | Result |
| fops.c | `mr_l1/l2/cond`, `mr_tx/ty`, `mr_* atomics`, `mr_target/value/lock/task`, `mr_fd/ready/policy/lock_slot` | 17-22 | Runtime route |
| fops.c | `tcp_punch_go/stop/phase/failed` | 158-162 | Runtime route |
| fops.c | `standard_io_backup[3]` | 507 | route 栈（P1 局部化） |
| util.c | `ks`, `mm_objs_per_slab`, `skb_buf`, `reclaim_sv`, `quarantined_reclaim_sv`, `prebuilt_reclaim_sv` | 5-10 | Runtime |
| util.c | `prebuilt_page_base…prebuilt_fake_fops` | 11-13 | Runtime prebuilt |
| util.c | `prepare_ctx/spray_ctx/pre_ctx/post_ctx`, `child_leak` | 14-18 | Runtime spray |
| util.c | `page_base`, `last_mm_struct`, `fake_lock/w0/task/parent/right/left/fops` | 27-35 | Runtime page |
| util.c | `pselect_custom_write/target`, `pselect_child_node` | 37-39 | WriteRequest |
| util.c | `p0_kernel_phys_load`, `g_init_cred_image` | 188,191 | Target |

### B.2 43284 与 bootstrap

| 符号 | 行 | 去向 |
|---|---|---|
| `libcxx_ko_target` | exp.c:24 | Runtime（shellcode data 内指针） |
| `g_encap_port`, `g_sender_port`, `g_spi`, `g_aes_key[32]` | exp.c:26-29 | Config |
| `g_seq` | exp.c:30 | Runtime（每次写递增） |
| `g_libcxx_r` | exp.c:41 | Runtime 恢复记录 |
| `KSUD_STAGE/KSUD/PREFS_PATH/MODULES_DIR` | bootstrap.c:12-15 | bootstrap 常量（非核心，P6 再议） |

### B.3 资源 owner/终结点

完整逐项表（每个 fd/pid/mmap/thread/signal，含 `mr_fd`、`punch_fd`/`map`、`listener`/`client_fd`/`server_fd`、`pipefd`、`perf_fd`/mmap、43284 `sk_send`/`file_fd`/`pfd`）见 [43499-43284-resource-ownership.md](43499-43284-resource-ownership.md)。本附件只保留全局清单。

**顺序**：waiter 停止访问 → route_done → unlock chain → join 全部线程 → 关闭 reclaim/socket → 释放页/映射；PI waiter 的“停止访问 → 解除关联 → 回收”逐字保留。

## C. 字面量盘点（P0 生成精确 TSV）

| 文件 | 数量 | 主要类别 |
|---|---:|---|
| main.c | 118 | 重试/超时、perf、脚本、路径、协议字符 |
| fops.c | 52 | spin/attempts、zc 帧偏移、pselect nfds |
| util.c | 58 | 页/喷溅、cred/payload 偏移、超时 |
| exp.c | 92 | port/marker、16/24/40 帧、hook 掩码、轮询 |
| libcxx.S | 26 | syscall/flags、路径、uid/tid |
| splicehelper.c | 14 | fd 0/1、16、exit codes |
| bootstrap.c | 13 | 路径、4096/16384、BLKROSET |
| target.h/common.h/runtime_struct_offsets.h | 88/46/32 宏 | 地址、布局、超时/尺寸 |

P0 命令（输出 `build/analysis/literals.tsv`）：`python3 tools/glk_literal_inventory.py`；它屏蔽注释/字符串后按文件/行/值/类(A–E)/上下文输出，`?` 是 P1 人工分类工作单。P0b 实测 2026-10-21：**2,558 条**（A 163 / B 236 / C 220 / D 33 / E 1,246 / L 660 / **? 0**）；其中 104 条为人工覆盖（`source=manual`）。E 按具名类别：AES 619、KernelSnitch 243、ABI 名 106、ELF 92、JSON 66、asm 25、log 24、HMAC 18、宏 13、wire/sentinel/tag 14；`offsets_json.c:13` 的 1 MiB 缓冲已归 B，不是 E。E 不是可运行期参数。高风险样例：`240`s、`4` attempts、`60000`、`50000/100000`us、`320` nfds、`100000000` spin、`0x5800/0x6800`、`0x1080`、`0x0f<<56`、`0x14000000` hook 掩码、`63/64` 路径容量。E 类只保留 syscall 号、ELF/AES 算法常量、SBOX；其余必须进 Target/Config 或 Runtime。
