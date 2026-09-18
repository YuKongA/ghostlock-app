# Native C 函数标准化说明

每个条目依次记录“用途”、“状态/输入输出”和“调用/清理”。系统调用只在影响资源所有权或控制流时列出。

## `main.c`

### 配置、环境与入口

| 函数 | 用途 | 状态/输入输出 | 调用与清理 |
|---|---|---|---|
| `validate_offsets_profile()` | 验证公共及路线特有偏移 | `kernel_offsets*` → bool | 不修改profile |
| `publish_active_offsets()` | 从只读profile view发布结构化地址快照及兼容镜像 | `TargetProfile` → `ResolvedAddresses` | 调 `resolved_addresses_init()` |
| `try_external_offsets()` | 尝试加载与release匹配的JSON profile | release → bool；写external profile | `load_offsets_json()` → `validate_offsets_profile()` |
| `select_offsets()` | 按精确 `uname -r` 选外部或内置profile | 写 `active_offsets` | 成功后 `publish_active_offsets()` |
| `timer_reset()` | 重置顶层阶段计时 | 写 `t0` | 无资源 |
| `timer_ms()` | 返回相对耗时 | 读 `t0` → ms | 无资源 |
| `init_cpu_config()` | 选主CPU和consumer CPU | 读环境/sysfs，写 `g_core_*` | 局部fd即时关闭 |
| `init_runtime_paths()` | 设定工作目录、日志与root脚本路径 | 读 `GHOSTLOCK_HOME`，写路径缓冲 | 无持久fd |
| `write_root_script()` | 生成后续root/KernelSU调度脚本 | 读运行路径，写文件 | 在函数内关闭文件 |
| `kernelsu_module_loaded()` | 检查KernelSU类模块状态 | `/proc/modules` → bool | `FILE*` 局部关闭 |
| `main()` | native ELF入口 | argc/argv → exit code | 仅调 `run_exploit()` |

### PI竞争与路线调度

| 函数 | 用途 | 状态/输入输出 | 调用与清理 |
|---|---|---|---|
| `waiter_thread()` | 建立PI waiter，触发UAF窗口后调用选定路线 | 读写 `f_*`、`waiter_*`、`route_done` | futex；分流到三个 `do_*_route()`；5.x额外disarm |
| `owner_thread()` | 持有目标PI futex并建立owner/waiter依赖 | 读写 `owner_*`、`f_pi_*` | 退出前unlock |
| `consumer_thread()` | 从另一CPU改变waiter优先级，促使PI链遍历 | 读写 `punch_consume_*`、`consumer_*` | `sched_setattr_tid()`；失败时使用futex备用刺激 |
| `reset_main_route_state()` | 重置一次竞争的 futex 与同步量 | 调 `PiRace::reset()`；保留 `fast_repair` latch | 无资源操作 |
| `PiRace::run()` | 等 waiter/owner 就绪、`CMP_REQUEUE_PI`、等 `route_done` 并返回结果 | 读 `route_status` 与 consumer counts | 定义在 `main.cpp`；等待无超时（`PI-TIMEOUT-01`） |
| `run_main_route_threads()` | 创建、同步、触发并 join 三个竞争线程 | `PiRace::start_threads/run/request_stop/join`；返回路线是否 OK | `PthreadOwner` 拥有线程；创建失败按 consumer→owner 清理 |
| `do_one_write()` | 一次抽象内核写入的总入口 | target/mode/leaf → bool；写payload模式 | `prepare_good_kernel_page()` → `run_main_route_threads()`；5.x resident特例 |
| `retry_write_stage()` | 为W1/W2/W3提供重试和回调验证 | 阶段参数+验证函数 → bool | 调 `do_one_write()`；5.x W2预构造修复页 |

### 阶段检测、victim与落地

| 函数 | 用途 | 状态/输入输出 | 调用与清理 |
|---|---|---|---|
| `check_selinux_off()` | 读SELinux enforce状态 | 无 → bool | 局部fd关闭 |
| `enforce_readable()` | 判断当前身份能否读enforce | 无 → bool | 局部fd关闭 |
| `process_has_seccomp()` | 解析当前进程Seccomp状态 | `/proc/self/status` → bool | `FILE*` 关闭 |
| `slab_drain()` | 以分配活动降低旧slab对下次喷射的影响 | 无 | 临时映射在函数内释放 |
| `perf_find_task()` | 从perf观测中取得victim `task_struct` 地址 | 无 → kernel address | perf fd/ring mapping局部回收 |
| `park_rooted_child()` | 保留一个已获得所需凭据的子进程 | 无返回 | 长期 `pause()` |
| `child_main()` | victim管道协议：报告task/UID、执行探针和最终交接 | `child_pipes*` | `perf_find_task()`；关闭未使用pipe端；可fork/exec |
| `spawn_child()` | 创建victim并建立管道 | 写 `child_pipes` → PID | child分支调 `child_main()` |
| `spawn_victim()` | 包装创建victim并读取task地址 | pipes + task_out → PID | 调 `spawn_child()`，关task read端 |
| `verify_selinux_stage()` | W1验证回调 | context → bool | `check_selinux_off()` |
| `verify_w2_stage()` | W2验证回调 | pipe context → bool | 命令victim报告UID |
| `verify_seccomp_probe_stage()` | W3验证回调 | pipe context → bool | 命令victim执行受Seccomp影响的探针 |
| `verify_leaf_dir_stage()` | 判断pselect leaf写的相对落点 | `w3_stage_context` → bool | 通过victim `comm`观测 |
| `run_exploit()` | 总控制器：初始化、profile、W1/W2/W3、重试、落地和收尾 | argc/argv → exit code；读写几乎全部全局状态 | 顶层所有权不完全集中；结尾调 `kernel5_resident_stop()` |

## `util.c`

### 运行期与地址辅助

| 函数 | 用途 | 状态/输入输出 | 调用/清理 |
|---|---|---|---|
| `ms_since()` | 局部耗时计算 | timespec → ms | 无资源 |
| `set_pselect_write_mode()` | 设置下一次payload的target/mode | 写 `pselect_custom_*` | 由 `clear_*` 对称重置 |
| `clear_pselect_write()` | 清除payload写模式 | 重置 `pselect_custom_*` | 无资源 |
| `tcp_route_selected()` | compact waiter的TCP/pselect静态选路 | profile+环境 → bool | 每次重读环境 |
| `kernel5_route_selected()` | 检查5.x Multicast能力 | profile → bool | 只读 |
| `read_first_line()` | 读proc/sysfs单行文本 | path/buffer | 局部fd关闭 |
| `log_startup_context()` | 输出UID、Seccomp、CPU和内核环境 | 只读系统状态 | 调 `read_first_line()` |
| `disable_rseq_for_thread()` | 关闭当前线程rseq | 当前线程 | 线程入口调用 |
| `futex_op()` | futex syscall薄封装 | futex参数 → long | 无额外所有权 |
| `sched_setattr_tid()` | 调整指定TID的nice/调度属性 | tid/nice → long | consumer主要触发点 |
| `init_p0_profile()` | 输出已解析物理地址快照诊断 | 读 `g_resolved_addresses` | 不再初始化或修改地址 |
| `p0_data_alias()` | 兼容地址换算入口 | image address → direct-map alias | 委托 `resolved_addresses_data_alias()` |
| `data_addr()` | 当前调用链的兼容地址入口 | image address → runtime alias | 调 `p0_data_alias()` |

## `address_space.c`

| 函数 | 用途 | 状态/输入输出 | 调用与清理 |
|---|---|---|---|
| `detect_target_soc()` | 从 Android 属性识别 QCOM、MTK 或 XRing | 属性区 → `TargetSocFamily` | 文件内纯读取，无资源 |
| `resolved_addresses_init_for_soc()` | 按显式 SoC/profile推导物理加载地址和 `init_cred` image | `TargetProfile` → `ResolvedAddresses` | 确定性计算，供固定向量验证 |
| `resolved_addresses_init()` | 设备入口：检测 SoC 后创建地址快照 | `TargetProfile` → `ResolvedAddresses` | 调前两项，无持久资源 |
| `resolved_addresses_data_alias()` | 将内核 image 地址转换为 direct-map alias | snapshot + image → alias | 纯函数 |
| `resolved_addresses_soc_name()` | 生成兼容诊断标签 | snapshot + profile → 字符串常量 | 无资源 |
| `put64()` / `put32()` | 按偏移向payload缓冲写整数 | buffer/off/value | 不分配资源 |
| `fill_profile_cred_copy()` | 按profile填充credential复制payload | buffer/off → bool | 读 `active_offsets` |

### kernelsnitch适配和堆资源

| 函数 | 用途 | 状态/输入输出 | 调用/清理 |
|---|---|---|---|
| `setup_kernelsnitch()` | 零调用兼容适配：创建地址碰撞探测上下文 | 写 `ks` | `kernelsnitch_context_init()`；登记 `COMPAT-01` |
| `kernelsnitch_collisions_ready()` | 零调用兼容适配：查询有效碰撞数 | `ks` → bool | `kernelsnitch_context_has_collisions()`；登记 `COMPAT-01` |
| `current_kernelsnitch_mm_struct()` | 零调用兼容适配：取得当前候选 `mm_struct` 地址 | `ks` → address | `kernelsnitch_context_result()`；登记 `COMPAT-01` |
| `cleanup_kernelsnitch()` | 零调用兼容适配：保留结果并回收KS上下文 | `ks` → address | `kernelsnitch_context_result/destroy()`；登记 `COMPAT-01` |
| `clone_child()` | 创建用于分配 `mm_struct` 的子进程 | 无 → PID | 调用者负责kill/wait |
| `clone_leak_child()` | 创建泄露/固定用子进程 | 写 `child_leak` → PID | 后续cleanup回收 |
| `open_memfd()` | 为子进程相关分配准备memfd | child → fd | fd所有权交给调用者 |
| `kill_child()` | 终止并wait子进程 | PID | 完成进程回收 |
| `clone_memfd()` | 组合child与memfd创建 | 无 → fd | 内部调clone/open |
| `close_reclaim_sockets()` | 关闭当前SKB回收socket | 写 `reclaim_sv` | 幂等倾向 |
| `quarantine_reclaim_sockets()` | 隔离可能仍引用污染页的socket | current → quarantined | 5.x W1修复前不释放 |
| `release_quarantined_reclaim_sockets()` | 修复后释放隔离socket | 写quarantine数组 | close |
| `stash_prebuilt_page()` | 暂存已喷射的修复页和fake地址 | current → `prebuilt_*` | 转移所有权 |
| `activate_prebuilt_page()` | 激活预构造页为当前payload | `prebuilt_*` → current | 状态交换 |
| `discard_prebuilt_page()` | 放弃预构造页 | 清 `prebuilt_*` | 关闭关联socket |
| `close_ctx_memfds()` | 关闭 `mm_ctx` 的memfd集合 | `mm_ctx*` | close后重置 |
| `free_ctx_storage()` | 释放 `mm_ctx` 数组 | `mm_ctx*` | free并清空 |
| `cleanup_page_prepare_state()` | 收束一轮堆塑形上下文 | 多组ctx、child、memfd、buffer | 调close/free/kill |
| `prepare_ctxs()` | 按slab布局分配prepare/spray/pre/post数组 | 写四个 `mm_ctx` | 由cleanup释放 |
| `prepare_skb_payload()` | 构造fake lock/waiter/task/fops及阶段数据 | base → bool；写 `fake_*`/buffer | 调put/fill；依赖TCP/pselect选路 |
| `prepare_kernel_page()` | 组织mm分配、碰撞定位、SKB回收和payload塑形 | 无 → `page_base` | 串联本组几乎全部函数 |
| `prepare_good_kernel_page()` | 对页准备增加重试和合法性检查 | 无 → address | 重复 `prepare_kernel_page()` |

## `fops.c`

### 5.x Multicast

| 函数 | 用途 | 状态/输入输出 | 调用/清理 |
|---|---|---|---|
| `fops_elapsed_ms()` | 路线局部计时 | timespec → ms | 无资源 |
| `mr_intr()` | resident waiter的信号处理器 | signal | 空处理，用于打断路径 |
| `mr_adjust()` | 切换调度策略以触发PI调整 | 写 `mr_policy` → syscall result | 无fd |
| `mr_stamp()` | 构造并提交Multicast option buffer | target/value/lock | `setsockopt(MCAST_BLOCK_SOURCE)` |
| `mr_y()` | resident waiter与重喷线程 | 大量 `mr_*` 原子状态 | 创建socket，退出前disarm/close |
| `mr_x()` | resident owner线程 | `mr_l1/l2` | futex lock/unlock |
| `kernel5_resident_start()` | 建立长期存活的Multicast writer | 无 → bool；写 `mr_*` | 创建两线程，requeue+信号 |
| `kernel5_resident_write()` | 更新目标/值并触发resident写 | target/value → bool | `mr_adjust()`，由 `mr_y()` stamp |
| `kernel5_resident_stop()` | disarm、join并释放resident资源 | 清 `mr_ready` | join、close reclaim、cleanup page |
| `do_kernel5_fake_lock_route()` | 非resident单次Multicast覆盖 | 写route/consumer状态 | 局部socket关闭 |

### TCP Zerocopy

| 函数 | 用途 | 状态/输入输出 | 调用/清理 |
|---|---|---|---|
| `tcp_wait_for_consumer_idle()` | 停止consumer并等正在执行的调用返回 | consumer原子量 | 自旋等待 |
| `tcp_make_pair()` | 建loopback TCP client/server对 | out fd → status | listener始终在函数内关闭 |
| `tcp_punch_thread()` | 对memfd循环填充/打孔 | `tcp_punch_*` | 不拥有fd，使用传入state |
| `do_tcp_fake_lock_route()` | TCP路线完整控制器 | 读fake/page，写route/consumer/punch状态 | 统一 `out` 停线程、join、munmap、close |

### pselect/select

| 函数 | 用途 | 状态/输入输出 | 调用/清理 |
|---|---|---|---|
| `route_delay_usec()` | 返回consumer相对select进入时机 | attempt → usec | 当前为固定策略 |
| `fdset_put_word()` | 向 `fd_set` 写64位word | set/index/value | 纯buffer写 |
| `fdset_get_word()` | 从 `fd_set` 读64位word | set/index → value | 纯buffer读 |
| `pselect_words_per_set()` | 计算每个fd_set可承载word数 | 无 → int | 纯计算 |
| `pselect_put_global_word()` | 把逻辑word映射到in/out/ex之一 | sets/index/value → bool | `fdset_put_word()` |
| `pselect_waiter_shift()` | 取profile的waiter栈布局滑移 | 无 → int | 读 `active_offsets` |
| `pselect_put_waiter_word()` | 应用滑移后写waiter word | sets/index/value | `pselect_put_global_word()` |
| `open_selected_fds()` | 按fd_set布置真实fd | sets/read/write fd | 创建的复制fd由路线/进程收尾 |
| `reserve_standard_io()` | 备份0/1/2以防路线覆盖日志fd | 写 `standard_io_backup` | `dup` |
| `restore_standard_io()` | 恢复并关闭标准fd备份 | 清backup | `dup2/close` |
| `prepare_pselect_fdsets()` | 构造compact或tree waiter的fd_set布局 | in/out/ex | 调word写辅助 |
| `do_pselect_fake_lock_route()` | 建阻塞fd、进入select/pselect、协调consumer并收尾 | 写route/consumer状态 | consumer stuck时故意保留fd并标记dirty |

## `offsets_json.c`

| 函数 | 用途 | 调用关系 |
|---|---|---|
| `json_skip_ws()` | 跳过JSON空白 | 所有parser helper |
| `json_match_key()` | 匹配成员名并推进指针 | `json_member_value()` |
| `json_skip_value()` | 跳过一个JSON值 | `json_member_value()`/loader |
| `json_member_value()` | 查找对象第一层成员 | 调skip/match |
| `json_read_string()` | 读字符串和简单转义 | `load_offsets_json()` |
| `json_parse_int()` | 解析十/十六进制和负整数 | `fill_external_entry()` |
| `store_profile_scalar()` | 按字段宽度写profile | `fill_external_entry()` |
| `fill_external_entry()` | 将JSON symbols/fields/scalars填入 `kernel_offsets` | 调member/parse/store |
| `load_offsets_json()` | 读文件、匹配release并输出profile | 顶层JSON入口；文件fd在内部关闭 |

## kernelsnitch、hash与系统helper

### `kernelsnitch.h`

| 函数 | 用途 |
|---|---|
| `__futex()` | kernelsnitch内部futex syscall封装 |
| `__do_increase()` | 碰撞放大工作线程入口 |
| `__increase()` | 协调指定工作线程增加futex负载 |
| `__compare()` | 排序碰撞候选 |
| `__measure()` | 测量候选futex地址的时间特征 |
| `__mm_candidate_matches()` | 检查候选是否符合目标 `mm_struct` 特征 |
| `__mm_mark_found()` | 原子记录成功地址 |
| `__mm_leak()` | `mm_struct` 候选扫描线程入口 |
| `__run_mm_leak_pass()` | 执行一轮canonical/tag sweep |
| `kernelsnitch_context_init()` | 分配 mmap 共享上下文，初始化显式 futex hash 策略、扫描存储和线程槽 |
| `__collision_pool_limit()` | 计算候选池上限 |
| `__screen_collision_pool()` | 初筛潜在hash碰撞 |
| `__prove_collision_pool()` | 对候选做时序复测 |
| `__verify_collision_pool()` | 组织候选验证过程 |
| `__collision_pass()` | 完成一次筛选和证明pass |
| `kernelsnitch_context_find_collisions()` | 在显式上下文中运行碰撞发现并更新状态 |
| `kernelsnitch_context_has_collisions()` | 只读查询碰撞发现结果 |
| `kernelsnitch_context_scan()` | 使用显式 hash context 扫描 `mm_struct` 候选 |
| `kernelsnitch_context_result()` | 在 destroy 前只读取得泄露地址 |
| `kernelsnitch_context_destroy()` | 释放上下文拥有的映射和数组，不隐式返回结果 |
| `kernelsnitch_param()` | 带全部参数的主执行入口 |
| `kernelsnitch()` | 使用默认参数的兼容入口 |

### `futex_hash.h`

| 函数 | 用途 |
|---|---|
| `rol32()` | 32位循环左移 |
| `jhash2()` | Jenkins word-array hash |
| `__jhash_nwords()` | 1–3 word hash核心 |
| `jhash_1word()` / `jhash_2words()` / `jhash_3words()` | 固定长度hash包装 |
| `futex_hash_no_trunc()` | 计算未截断futex key hash |
| `__futex_hash()` | 按hash表大小截断 |
| `futex_hash_context_init()` | 校验并保存显式、不可变的hash表大小 |
| `futex_hash_context_key()` | 按显式context截断已构造的futex key |
| `futex_hash_context_bucket()` | 从地址/mm构造private key并按显式context返回bucket |
| `futex_init()` | 根据CPU数初始化估算hash表大小 |
| `futex_hash()` | 旧隐式全局表大小入口，保留作兼容包装 |

### `utils.h` / `timeutils.h`

| 函数 | 用途 |
|---|---|
| `pin_to_core()` | 固定当前线程CPU |
| `reset_cpu_pin()` | 恢复CPU亲和范围 |
| `set_limit()` | 提高fd与进程软限制 |
| `set_unbuffer()` | 禁用标准流缓冲 |
| `set_proc_name()` | 设置进程/线程名 |
| `gettime_ns()` | 读单调时钟纳秒值 |
| `write_file()` | 向proc/sys类文件写字符串 |
| `set_user_namespace()` | 创建new user/net namespace并填UID/GID映射 |
| `hexdump()` | 调试输出缓冲内容 |
| `parse_ul()` / `parse_xl()` | 自动进制/十六进制整数解析 |
| `rdtsc_begin()` / `rdtsc_end()` | 高精度测量边界 |

## 主要工程结论

- `run_exploit()`是阶段、选路、victim协议和恢复策略的集中点。
- `prepare_kernel_page()`与 `prepare_skb_payload()`通过 `page_base`/`fake_*`/多组ctx形成最强堆状态耦合。
- `PiRace::run()` 提供公共 PI 竞争并返回结构化结果，但路线仍反向读写 `PiRace` 的共享原子量（会话内收编见 CPP12）。
- TCP局部清理最集中；pselect允许dirty fd保活；Multicast的disarm、quarantine和W1/W2主流程交织最深。
