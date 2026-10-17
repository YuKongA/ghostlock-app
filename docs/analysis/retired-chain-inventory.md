# 退役链清点（成功历史基准）：CVE-2026-43284 真机 PASS 链

> 清点对象＝**成功跑通的那棵树**：git `ref` = `6f7ded87`（2026-10-04，"43284 full chain PASS with self-built LKM"，
> 前序提交 `da517110`(hook 接线) 与 `e29771c4`(LKM 收尾 restore_enforce)）。该树源码根为 `src/core/…`；
> 当前工作树（dev-nocpp）已是 `core/src/…` 新布局，同名不同代。**源码证据**一律以 `git show 6f7ded87:<path> | nl -ba`
> 的行号为准；**门禁证据**引用 `docs/analysis/device-gates/**`（该目录在 ref 与当前工作树均存在）。只读清点，未改其它文件。

## 0) 基准与范围

| 项 | 值 | 证据 |
|---|---|---|
| 成功修订 | `ref` = 6f7ded87（"43284 full chain PASS with self-built LKM"） | `git log -1 6f7ded87` |
| 链所在文件 | `stage_runner.cpp/.hpp` · `real_ops.cpp/.hpp` · `backend_terminal.cpp/.hpp` · `steps/chain.cpp/.hpp` · `pagecache/pagecache.cpp` | `git ls-tree -r --name-only 6f7ded87` |
| 设备 | A301SO / `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`，冷机后 KernelSU 未加载 | `docs/analysis/device-gates/B5-9h-20261004-full-chain-lkm-pass.md:4` |

## 1) 阶段与顺序（写什么、按什么顺序写）

两个入口：**分阶段** `run_stage_cli`（`--run-cve-2026-43284 <ko> <target> --stage=plan|write|trigger|full`，`stage_runner.cpp:825` 起，plan 只算不写 `:863-907`，写面为只读 stub `:195-198`）；**生产** `Cve2026_43284Policy::run` → `run_backend_terminal`（`backend_terminal.cpp:217-227` → `:38`）。

阶段机 `steps::ChainStage`：`steps/chain.hpp:107-121`。逐阶段：

| # | 阶段 | 写什么 | 证据 |
|---|---|---|---|
| 0 | ResolveTarget | 只校验载体路径（无写） | `steps/chain.cpp:357-379` |
| 1 | ComputePlan | 模块镜像 → 单 region `{offset=0,len=pad16(module)}`；plan 闭合断言（写前） | `stage_runner.cpp:250-307`；`steps/chain.cpp:381-398` |
| 2 | patch #1 PatchCrashDump | `pad16(splicehelper)`（1440 B / 90 块）写到 `crash_dump64 + 0`，再校验 offset 16 的 16 B 窗口 | `steps/chain.cpp:400-411`；`steps/crash_dump.cpp:29-67`；`steps/crash_dump.hpp:32-38` |
| 3 | carrier Write+Verify | 模块镜像 769 块（12304 B）写到载体 offset 0，**每块写后立即回读比对** | `steps/chain.cpp:161-269`（写 `:243`，回读 `:252-267`） |
| 4 | hook Hook | libc++ sentry 站点 4 B 分支 + 480 B 载荷 + 16 B trampoline（**先 shellcode 块，再 trampoline 块**） | `steps/chain.cpp:459-469`；`steps/hook_patch.cpp:165-191`；`steps/hook_patch.hpp:111-115` |
| 5 | trigger Trigger | double-fork 孤儿哨兵（不写字节） | `steps/chain.cpp:471-475`；`real_ops.cpp:346-389` |
| 6 | wait WaitResult | 轮询终态标记（不写字节） | `steps/chain.cpp:477-489`；`real_ops.cpp:391-432` |
| 7 | Cleanup | **无条件 hook 回滚 → release（关 fd + 清零 SA）→ 擦 journal** | `steps/chain.cpp:273-293` |

顺序约束（写死在代码里的因果）：patch #1 必须在 carrier 写之前（vendor 读桥要 exec 已打补丁的 crash_dump64）`steps/chain.cpp:400-403`；hook 必须在 carrier 校验之后、trigger 之前（上游顺序）`:452-470`；`--stage=write` 在 carrier 校验后、hook/trigger 前停 `:452-457`。

## 2) 全部常量（file:line）

**hook 站点**：目标 `/system/lib64/libc++.so` — `steps/hook_patch.hpp:44`（默认值 `stage_runner.hpp:302`）；符号 `_ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE6sentryC1ERS3_` — `steps/hook_patch.hpp:41-42`（默认 `stage_runner.hpp:303`）；守卫策略默认 `HookGuardPolicy::Skip`（+4）— `stage_runner.hpp:304`，语义 `steps/elf_hook.hpp:152-159`。
guard 字常量：`kAarch64Paciasp=0xD503233F` / `kAarch64Pacibsp=0xD503237F` / `kAarch64BtiC=0xD503245F` / `kAarch64BtiJ=0xD503249F` / `kAarch64BtiJc=0xD50324DF` — `steps/elf_hook.hpp:49-53`。
运行时实测：`hook_vma=0xb70ac`、`hook_offset=0xb30ac`、`hook_steal=0xf9400028`、`guard_inst=0xd503245f`、`guard_skipped=1` — `docs/analysis/device-gates/B5-9h-20261004-plan-pass.md:19`；符号原始 VMA `0xb70a8` — `docs/analysis/device-gates/B5-9h-20261004-device-assets-recon.md:8`。

**payload / trampoline**：落点＝首个可执行 PT_LOAD 段尾 `payload_file_offset=file_offset+size`、`payload_vaddr=vaddr+size` — `steps/elf_hook.cpp:412-414`；`payload_max`＝max(BSS 尾, 页对齐尾并夹 EOF 与其它 PT_LOAD 映射) — `steps/elf_hook.cpp:249-331`（口径 `:240-248`）。
实测：`shellcode_len=480 shellcode_vma=0x102628 shellcode_offset=0xfe628 payload_max=2520`、`trampoline_len=16 trampoline_offset=0xb30a0 trampoline_pos=12` — `docs/analysis/device-gates/B5-9h-20261004-plan-pass.md:20-21`。
**entry 0x90 / 回跳 0x1D4 / displaced 0x1D0** — `steps/hook_patch.hpp:130-132`（libcxx_start / `b end` / libcxx_first_inst_copy）。trampoline 单位 16 B 与 displaced 哨兵 — `steps/hook_patch.hpp:36-38`；trampoline 按 16 对齐、块内位置=offset&15、且 `pos+4<=16` — `steps/hook_patch.cpp:137-153`。
shellcode 上限 4096 / 对齐 16 / 槽上限 16 — `steps/shellcode.hpp:33-35`；分支编码限 ±128 MiB — `:112-115`。

**blob（尺寸 / sha256）**：libcxx.S 载荷 472 B → 零填充 480（16 对齐），sha256 `d226d2e7883d3eba60d4e12f6707b2830c11a4a1d2e1e492d01bac6fdd245aa7` — `embed/libcxx_blob.hpp:35-38`（来源/构建 `:1-28`）；splicehelper 1432 B → 1440，sha256 `3c0a108e637954c114df21f03bda95eb691e66f60333b9d78439d9550482545c` — `embed/splicehelper_blob.hpp:31-34`；PASS 用自建 `.ko` 12304 B / `d557fe11…` — `docs/analysis/device-gates/B5-9h-20261004-full-chain-lkm-pass.md:10`（惰性版 11672 B / `9f3d85f3…` — `…-lkm-inert-load-pass.md:13`）。

**槽表（7 槽，偏移/容量/种类）** — `steps/hook_patch.cpp:229-237`（符号表注释 `:223-228`）

| # | 槽 | 偏移 | 容量 B | 种类 |
|---|---|---|---|---|
| 0 | mutex_file | 0x00 | 8 | ByteString |
| 1 | selinux_ctx | 0x08 | 23 | ByteString |
| 2 | selinux length（MOVZ W2） | 0x16C | 4 | Instruction |
| 3 | attr_exec | 0x1F | 21 | ByteString |
| 4 | exe_path | 0x34 | 19 | ByteString |
| 5 | ko_target | 0x47 | 64 | ByteString |
| 6 | displaced inst copy | 0x1D0 | 4 | Instruction |

索引常量 `kLibcxxSlotMutex…kLibcxxSlotDisplaced`=0..6、`kLibcxxSlotCount=7`、`kLibcxxValueSlotCount=6` — `steps/hook_patch.hpp:133-141`；值槽实填 6 个（displaced 由 plan 追加）— `steps/hook_patch.cpp:313-321`；容量检查＝非空且 `size < capacity`（ByteString 留 NUL）— `:248-260`；selinux 长度立即数＝`context_len+1`（含 NUL）打包 MOVZ — `:302-311`。

**5 个绑定字符串及默认值**（结构 `steps/hook_patch.hpp:151-157`）

| 绑定 | 默认值 | file:line |
|---|---|---|
| `mutex_path` | `/dev/df` | `steps/hook_patch.hpp:146` |
| `selinux_context` | `u:r:vendor_modprobe:s0` | `steps/hook_patch.hpp:144` |
| `attr_exec_path` | `/proc/self/attr/exec` | `steps/hook_patch.hpp:147` |
| `insmod_path` | `/vendor/bin/insmod` | `steps/hook_patch.hpp:145` |
| `carrier_path` | 无默认：运行期由载体路径绑定 | `real_ops.cpp:285-300`；`stage_runner.cpp:352-368` |

**载体选择校验（valid_carrier_path / vendor 判定）**：`valid_carrier_path`＝绝对路径 + 长度 <64 B + 无空白/控制符，**且** `/vendor/` 或 `/system/vendor/` 前缀 — `steps/chain.cpp:19-42`；`valid_dev_carrier_path`（仅 `--allow-dev-target`）＝去前缀要求 — `:44-46`；放行 `carrier_path_allowed` — `:99-103`；`is_vendor_path`（读桥/helper 判定）— `steps/crash_dump.cpp:69-72`。默认 4 个 `/vendor/lib64/…` 载体表（顺序即上游）— `steps/chain.hpp:70-75`；profile 载体 token 0..4 与解析 — `backend_terminal.hpp:39-53`、`backend_terminal.cpp:20-36`；去重/上限/回退 — `steps/chain.cpp:48-63`、`:65-77`；回退只在「未写一字节前的旧块读失败」— `steps/chain.hpp:306-313`、`steps/chain.cpp:436-441`。

**mutex 与终态标记路径**：成功 `/dev/dfm0` — `real_ops.hpp:56`；失败 `/dev/dfm1` — `:57`；**非终态** mutex `/dev/df`（module loading in flight，继续轮询）— `:58-60`；模块目录 `/sys/module/dirtyfrag`（present→gone ⇒ 已完成并自卸载）— `:61-63`。与 hook 载荷的 `/dev/df` 槽同值 — `steps/hook_patch.hpp:146`。

**可选 exec_path 与触发延时**：`exec_path` 默认 `nullptr`（不 exec，孤儿退出即触发）— `real_ops.hpp:68`，使用点 `real_ops.cpp:365-374`（先 `usleep(delay*1000)` 再可选 `execl`）；触发延时默认 **1000 ms** — `real_ops.hpp:69`，分阶段入口覆写 **500 ms** — `stage_runner.cpp:999`；等待预算默认 **5000 ms** — `steps/chain.hpp:196`，分阶段硬编码 `request.wait_timeout_ms = 5000U` — `stage_runner.cpp:860`，`BackendTerminalDeps::wait_timeout_ms = 5000U` — `backend_terminal.hpp:71`；模块尺寸界 64 B / 64 MiB — `stage_runner.cpp:30-31`；镜像上限 `kElfMaxImageBytes=64 MiB` — `steps/elf_hook.hpp:36`。

## 3) 写管线（顺序 / 单位 / 对齐 / 校验 / 回滚 / ESP 组装 / 序号纪律）

**单位与对齐**：单位 **16 B** — `steps/chain.hpp:51`、`pagecache/pagecache.hpp:39`、`steps/crash_dump.hpp:36`、`steps/hook_patch.hpp:36`；模块镜像零填充到 16 的倍数 `padded=((n+15)/16)*16` — `stage_runner.cpp:282-283`；plan 闭合（len 必须 16 对齐；落在 plan.extent / 载体声明 size / 目标文件 size 内；region 不重叠）— `steps/chain.cpp:299-344`；目标真实尺寸取 `fstat(2)`，写前逐 region 断言 — `stage_runner.cpp:980-993`，`TargetOutOfBounds` 语义 `steps/chain.hpp:129-131`。

**先写谁后写谁**：① patch #1（crash_dump64@0，90 块）`steps/chain.cpp:403-411`；② carrier 镜像（region 循环：先 journal 读旧块 → `write16` → 回读比对）`:175-269`；③ hook（先 shellcode 块，再 trampoline 块）`steps/hook_patch.cpp:176-190`；④ trigger → wait → cleanup `steps/chain.cpp:471-490`。

**写后校验**：carrier 每块写后 `read_block` 比对（不符 `VerifyMismatch` + 回滚）— `steps/chain.cpp:252-267`，`verified` 语义＝`blocks_verified==blocks_written` — `stage_runner.cpp:592-594`；patch #1 写完全部块后校验 offset 16 的 16 B，**短读按 upstream「verify skipped」放行** — `steps/crash_dump.cpp:52-65`；hook 在 plan 阶段先读回 shellcode 原区与 trampoline 原块作为回滚依据 — `steps/hook_patch.cpp:124-153`。

**restore / 回滚路径**：链内 journal 有界 128 块（溢出记 `journal_overflow`；不能回滚的大 region 设 `rollback=false`）— `steps/chain.hpp:53-58`，回滚「新→旧」— `steps/chain.cpp:136-153`；终结点顺序（**每条路径**）：停写/校验 → 已写块尽力回滚 → **hook restore** → `release` 恰好一次 → 擦 journal — `steps/chain.hpp:32-37`、`steps/chain.cpp:273-293`；hook restore 顺序：**先 trampoline 原块，再 shellcode 原区** — `steps/hook_patch.cpp:193-220`，只有「欠着一次 restore」才返回 true — `steps/hook_patch.hpp:117-122`、`real_ops.cpp:328-344`；实测 `hook_restored=1` — `docs/analysis/device-gates/B5-9h-20261004-full-chain-lkm-pass.md:28`。

**ESP 报文组装（头 / IV / 旧页 / ICV）**：布局 `SPI(4) | Seq(4) | IV(16) | Ciphertext(16) | ICV(icv_len)`，偏移 0/4/8/24/40 — `ipsec/ipsec.hpp:47-55`；尺寸 `8+16+16+16=56` — `:37-42`；`IV = AES256_ECB_DEC(K, old) XOR desired` — `:57-58`；SA 字段 `aes_key[32]` / `hmac_key[32]` / `icv_len=16(默认)` — `:28-35`。
流水（`write16`）：读旧块（该读即 CBC 密文）→ 算 IV → 填 SPI/Seq/IV 头区（`store_be32` 大端）→ `esp_compute_icv` → `pipe2` → vmsplice(头+IV) → splice 目标页进同一管道 → vmsplice(ICV) → 一次 splice 到已 connect 的 ESP socket，长度 `header+16+icv_len` — `pagecache/pagecache.cpp:168-268`（头区组合 `:17-19`）。
ICV＝HMAC-SHA256(`hmac_key`, header8||iv16||ciphertext16) 截断到 `icv_len`（先算全 32 B）— `ipsec/ipsec.hpp:66-77`、`ipsec/hmac_sha256.hpp:14-15`。旧块来源优先级：直接 `pread(file_fd)` → 失败且 vendor 路径则走 crash_dump 桥（**只有整 16 B 才接受**，其余 fail-closed 不发电文）— `pagecache/pagecache.cpp:116-166`，桥选择 `real_ops.cpp:78-90`、`:208-229`；载体打不开（`file_fd<0`）时由已打补丁的 helper 把目标页 splice 进同一管道（`use_helper=1`）— `pagecache/pagecache.cpp:225-238`、`real_ops.cpp:96-109`。

**同 SA 序号延续（纪律）**：`next_seq` 起点 1，**只在一条完整电文发送成功后 +1** — `pagecache/pagecache.hpp:69`、`pagecache/pagecache.cpp:255-261`；patch #1 用独立 page 上下文绑定 crash_dump64 fd，但**共享同一 SA/socket/io**：写前拷入 `next_seq`、写后拷回 — `real_ops.cpp:239-257`；hook apply 前 `hook_page.next_seq = page.next_seq`、apply 后拷回，restore 再重复一次 — `real_ops.cpp:303-307`、`:321-325`、`:336-342`。
为什么必须延续：SA 反重放窗口对整个 SA 共享，重置序号会被内核丢弃（设备实测根因）— `docs/analysis/device-gates/43284-hook-write-verified.md:19-21`（hook 窗口从 seq=1 起 ⇒ 报文被丢，改为接手前一窗口已达序号；成功样例 `:6-10`）。

## 4) 触发与等待

**trigger 形态**（`real_ops.cpp:346-389`）：`fork()` → 中间子进程**立刻 `_exit(0)`** `:375`（孙子被 init 收养）；孙子进程：可选 `usleep(trigger_delay_ms*1000)`（默认 1000 ms；分阶段入口覆写 500 ms）→ 若 `exec_path != nullptr` 则 `execl(exec_path, exec_path, NULL)` → `_exit(0)` — `:365-374`；父进程 `waitpid` 中间子进程（EINTR 重试）后置 `triggered=true` 返回 0 — `:377-384`；非 Linux 编成返回 `-ENOSYS` — `:385-388`；语义说明 `real_ops.hpp:12-16`。

**wait / terminus 判据**（`real_ops.cpp:391-432`）：前置 `device.available()` 为假 ⇒ 返回 `Pending`（不假装成功）`:397-399`；每轮 10 ms 轮询，顺序：**先查失败标记 `/dev/dfm1` ⇒ `Failed`**（陈旧成功标记不得掩盖新失败）`:404-407` → 查 `/dev/dfm0` ⇒ `LkmLoaded` `:408-410` → 查 `/dev/df`（记录但**不终态**，继续轮询）`:411-414` → 查 `/sys/module/dirtyfrag` 的 present→gone ⇒ `Failed` `:415-424` → 超时 ⇒ `Timeout` `:425-429`。判据枚举 `steps/chain.hpp:147-152`；链映射 `LkmLoaded ⇒ lkm_loaded`、`Failed ⇒ LkmFailed`、其它 ⇒ `WaitTimeout` — `steps/chain.cpp:482-489`；退出码（上游 nativeRunAll）：0 成功 / 1 ksud 失败 / 2 超时 / 3 patch-write-trigger 失败 — `stage_runner.cpp:790-800`（分阶段版 0/1/2/3/4/5/6 — `:801-823`）。

**LKM / UMH late-load 契约**：生产链在 `run_backend_terminal` 内：校验 steps token → 采设备事实（`f4c50a4` 已打补丁即拒）→ 解析 KMI/载体 token → 构造 UMH 命令 → 跑链 → **只有干净终态**（`error==None && lkm_loaded`）才填 `UmhForwardInput`（`lkm_loaded=true`、KMI label、carrier、**session_secrets 借出**、channel），随后 `StageResult::Continue` — `backend_terminal.cpp:129-156`、`:217-227`。命令形状 `{program, "late-load", flags…}`，**无 shell、不拼接字符串**、参数有界且查控制字节 — `lkm/lkm_image.hpp:85-93`；参数位 `package_name|ro_partitions|soft_reboot` — `lkm/lkm_policy.hpp:79-85`；KernelSU 包名 `me.weishu.kernelsu` — `lkm/lkm_image.cpp:343` 附近（`default_root_package`）；8 条 KMI 表（含本机 `android13-5.15`）— `lkm/lkm_policy.hpp:47-60`。
**期望输出行**（设备侧 root 自述，`/data/local/tmp/ghostlock-ksu.log`）：`[*] late-load kmi=android13-5.15 · late-load exit=0` → `[*] temp su uid=0; watching kernelsu.ko` → `[+] KernelSU module loaded` — `docs/analysis/device-gates/43284-terminal-criteria-verified.md:11-19`（同形 43499 版 `…/ANC-01-20261002-multicast-direct-pass.md:56-59`）。终局判据（对齐 B5-9h）：`uid=0` / `/proc/modules` 含 `kernelsu` / `getenforce=Enforcing` — `…/43284-terminal-criteria-verified.md:3-7`；实测行 `kernelsu 204800 1 - Live 0x0000000000000000 (OE)` — `:5`。

## 5) 该次真机门禁的期望结果原文（device-gates/**）

**B5-9h 全链 PASS（成功基准，2026-10-04）** — `docs/analysis/device-gates/B5-9h-20261004-full-chain-lkm-pass.md`

```
GLK_STATUS run.module path=/data/local/tmp/ghostlock.ko bytes=12304 wrote=1 verified=1 ko_vermagic=original
GLK_STATUS run.chain written=769 verified=769 rolled_back=0 cleanup=1 journal_overflow=1 rollback_incomplete=0 \
          crash_dump=1 hook_planned=1 hook_armed=1 hook=0 hook_restored=0 hook_error=None      <- write 阶段（:15-18）
GLK_STATUS run.trigger fired=1 wait_incomplete=0
GLK_STATUS run.wait outcome=LkmLoaded terminus=1 error=None                                  <- trigger 阶段（:29-31）
```

- 事后核验原文：`uid=0(root) gid=0(root) groups=0(root) context=u:r:ksu:s0` / `Enforcing` / `grep -c kernelsu /proc/modules → 1` — 同文件 `:37-42`。
- 链条自述（页缓存写 → hook → trigger → insmod → ksud late-load → 恢复 enforcing → `-E2BIG` 自卸载）— 同文件 `:45-51`。
- plan 阶段期望行（hook 几何 + `wrote=0`）— `…/B5-9h-20261004-plan-pass.md:15-22`；惰性加载期望行 `ghostlock: selinux_state set permissive` / `ghostlock: umh exec returned 0 …` — `…-lkm-inert-load-pass.md:34-38`；收尾 `ghostlock: selinux_state restored to enforcing` — `…-lkm-restore-enforce-pass.md:20-25`。

**后续同关键行（新布局树上的 2026-10-15 记录）** — `docs/analysis/device-gates/`：hook 写入并校验 `GhostLockNative: initialize: adopt seq=91 present=1 window=…` → `GhostLockNative: hook: write_hook wrote=1 refusal=(null)` → `typed: hook wrote=true code=0` → `deviceGate: PASS（38 项全部命中）` — `43284-hook-write-verified.md:4-11`；镜像写 + patch1 `write_image verified=1 refusal=(null)` / `typed: patch1 wrote=true helperBytes=1440` — `43284-first-write-verified.md:5-9`；等待终态实测 `phase 4 ok, /dev/dfm0 present after 1520 ms` — `43284-native-access-20261010-write-pass.md:64`。

## 6) 退役链的"形状记忆"（供新布局对照）

- 新树示例工具把该修订标为基准："Every target value below is taken from commit 6f7ded87, which is the run that PASSed on this device"，并逐条记下 wait 判据与 5000 ms 预算 — `core/src/tools/pagecache_load_lkm.cpp:4-13`。
- 同处七阶段划分（`credentials → module → patch1 → hook → write → trigger → wait`）与 Lua 版同序同默认（`block_bytes=16`、`trigger.delay_ms=1000`、`wait.budget_ms=5000`）— `procedures/pagecache_load_lkm.lua:6-7`、`:28-33`。
- profile 侧同名键（`execution.write.block_bytes` / `execution.trigger.delay_ms` / `execution.wait.budget_ms` / `execution.wait.poll_ms`）— `profile/profile-schema.json:127-146`。
