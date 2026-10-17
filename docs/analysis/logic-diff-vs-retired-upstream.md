# 43284 链：逻辑三方对照（工作树 / 6f7ded87 / 上游）

> 只读任务产物：未运行 adb、未跑 gradle、未做任何设备写入；git 只用 `git show` / `git grep` / `git log` / `git diff`。
> 路径约定：**A** = 工作树，相对 `core/src/backend/cve_2026_43284/`（另有 `jni/`、`app/`、`tools/` 从仓库根写）；**B** = `git show 6f7ded87:src/core/backend/cve_2026_43284/` 下对应文件；**C** = 上游 HEAD 的 `app/src/main/jni/exp.c`、`splicehelper.c`、`libcxx.S`、`dirtyfrag-lkm/dirtyfrag.c`。
> 另：`P` = `app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt`，`K` = `app/src/main/kotlin/com/ghostlock/app/jni/cve43284/Assembled43284.kt`（两者均从仓库根写）。
> 表按“能否导致 panic”降序；同一机制的多条差异合并为一行，展开见 §二。

## §一 汇总表

| # | 差异 | A（我们） | B（成功历史） | C（上游） | panic | 依据 |
|---|---|---|---|---|---|---|
| 1 | 钩子写顺序：站点块与载荷谁先 | **先写站点块（分支立刻生效），再写载荷** | 先 shellcode 全部块，最后写 trampoline（站点）块 | 先 patch shellcode，再 patch trampoline | **高** | A `steps/pagecache_steps.cpp:155-160` → `:178-194`；B `steps/hook_patch.cpp:176-189`；C `exp.c:716-740` |
| 2 | 钩子恢复（restore） | **全链无 restore**；只有 `.gl_restore` 文件存在时手写回一块原厂字节 | 终点无条件 restore（先于 release），成功/失败/早退都走 | cleanup 里 `restore_hook`（先 trampoline 后 shellcode）+ `fadvise_drop` | **高** | A `P:1278-1291`、`:1461-1486`；B `steps/chain.cpp:279-284` + `real_ops.cpp:328-344`；C `exp.c:745-753`、`:887-893` |
| 3 | 写失败的回滚 + 验证失败后的动作 | **无回滚**；验证不中即按 2 ms 循环**重发同一块**，200 ms 预算内最多约百次 | journal 保存旧块，失败即“新→旧”回写；验证只读一次 | 无回读、无回滚；只看 splice 返回值 | **高** | A `steps/pagecache_steps.cpp:301-311`、`:365-367`、`K:495`；B `steps/chain.cpp:136-153`、`:221-267`；C `exp.c:345-376` |
| 4 | vma↔file 换算来源 | **硬编码** site 0xB30AC/0xB70AC、landing 0xFE628/0x102628，并在 Kotlin 里再假定 `+0x4000` | 运行时从 ELF 导出：`payload_file=exec.file_offset+size`、`payload_vaddr=exec.vaddr+size` | 运行时 `find_hook_target` 返回 hook_off/shell_off | **高** | A `hook/sentry_hook.cpp:82-92`、`K:236-238`；B `steps/elf_hook.cpp:412-414`、`:355-360`；C `exp.c:681-682` |
| 5 | `/dev/df` 的语义（mutex 生命周期） | UMH 脚本**两次 `rm -f /dev/df`**，把一次性 mutex 变成可重入 | 从不删除；只创建 dfm0/dfm1 结果标记 | 载荷用 `openat(O_CREAT\|O_EXCL)` 建 mutex；App 只 stat，不删 | **高** | A `tools/lkm/ghostlock/root_cmd.sh:69-72`、`:105`；B `root_cmd.sh:59-70`（无 rm）；C `libcxx.S:56-66` + `exp.c:862-885` |
| 6 | trigger 前是否二次应用站点 | **触发前再写一次站点块** | 只 apply 一次 | 只 patch 一次 | 中 | A `P:1366-1384`；B `steps/chain.cpp:459-469`；C `exp.c:852-861` |
| 7 | 引爆者（谁让 init 走到被钩函数） | double-fork 之后**自己 exec `crash_dump32 1 1 11 1`** 去点火，默认延时 2000 ms | double-fork，可选 `exec_path`，否则纯 sleep | double-fork，孙进程 `sleep(1)` 后退出，靠 init 回收 | 中 | A `P:1409-1415`、`:1386`；B `real_ops.cpp:356-375`；C `exp.c:763-773` |
| 8 | UMH 脚本新增的 ksud 事件 | **post-fs-data / services(detached) / boot-completed 三段** | 只有 `late-load --kmi … --allow-shell` | `<ksud> && touch /dev/dfm0 \|\| touch /dev/dfm1`（无参数） | 中 | A `root_cmd.sh:87-97`；B `root_cmd.sh:55-57`；C `dirtyfrag.c:58-60` |
| 9 | 写前原值断言（preimage） | 无；只有钩子站点的 guard/displaced 两字 | 有：整 region 写前比对，失败即判 carrier 不可用 | 无 | 低 | A `steps/pagecache_steps.cpp:147-154`；B `steps/chain.cpp:187-213`；C `exp.c:344-362` |
| 10 | seq 递增时机 | 发送成功后才 `+1`；跨窗口 `adopt_sequence` | 发送成功后才 `+1` | `g_seq++` 在组帧时**先取号**，发送失败也消耗 | 低 | A `landing/pagecache_write.hpp:199-200`、`:270-275`；B `pagecache/pagecache.cpp:255-261`；C `exp.c:226` |
| 11 | 拼接 flags | vmsplice/splice 全 0 | 全 0 | vmsplice 用 `SPLICE_F_GIFT`、file→pipe 用 `SPLICE_F_MOVE` | 低 | A `landing/real_splice_io.cpp:253`、`:277`；B `pagecache/pagecache.cpp:48`、`:68`；C `exp.c:242`、`:265`、`:272` |
| 12 | UMH 的 argv 由谁定 | 脚本用退役 43499 形态 `late-load --kmi … --allow-shell`，**不经** B 的 argv 构造器 | C 侧构造 `{program, late-load, [--package-name P], [--ro-partitions], [--soft-reboot]}`（无 shell、无 --kmi/--allow-shell） | C 侧命令串只有裸 `<ksud>`；参数由它自带的 ksud 决定 | 低 | A `root_cmd.sh:75`；B `lkm/lkm_image.cpp:445-459`；C `dirtyfrag.c:58-60` |
| 13 | 执行域与 SELinux 上下文写入点 | helper 在 crash_dump 域；载荷期望 init tid=1 uid=0；ctx 写在孙进程 attr/exec；**另在 root 脚本 `chcon /dev/glk → null_device`** | 同左的 helper/载荷域；ctx 由 C 侧绑定（含 NUL 长度） | 同左；字符串静态在 .S 里 | 低 | A `landing/crash_dump_bridge.hpp:7-12`、`K:211-230`、`root_cmd.sh:24-25`；B `steps/crash_dump.cpp:76-79`、`hook_patch.cpp:287-316`；C `libcxx.S:14-25`、`:104-126` |
| 14 | 终态判据 | `/dev/dfm0`/`dfm1` + `/sys/module/kernelsu`（`module_marker` 未接线） | 四 marker + module 目录 + “见过又消失=自卸载” | `/dev/df`、`/dev/dfm0`、`/dev/dfm1` 三 marker | 低 | A `steps/pagecache_steps.cpp:221-252`、`jni/api_43284_device.cpp:183-196`；B `real_ops.cpp:400-432`；C `exp.c:862-885` |
| 15 | 帧布局与单位 | 16 B 块；`spi(4)+seq(4)+iv(16)+ct(16)+icv(icv_len)` | 同 | 同（注释即 `SPI+Seq+IV+ciphertext`） | 无 | A `landing/pagecache_write.hpp:34-38`、`:166-168`；B `pagecache/pagecache.cpp:17-19`、`:243-251`；C `exp.c:210-213`、`:225-230` |
| 16 | IV 计算 | `AES_ECB_DEC(K,old) XOR desired` | 同 | 同 | 无 | A `landing/esp_crypto.hpp:37-52`；B `ipsec/ipsec.cpp:34-47`；C `exp.c:146-151` |
| 17 | ICV 覆盖与截断 | 40 B = SPI+seq+IV+ct；截断到 `sa.icv_len`，0 或 >32 拒绝 | 40 B，同一顺序；0 或超上限拒绝 | 40 B（`hdr 8 + iv 16 + old_content 16`），截断到 `g_icv_len` | 无 | A `landing/esp_crypto.hpp:59-74`、`pagecache_write.hpp:190-191`；B `ipsec/ipsec.cpp:17-19`、`:54-84`；C `exp.c:232-238` |
| 18 | 槽位偏移/容量/内容 | 0x00/8、0x08/23、0x16C 长度、0x1F/21、0x34/19、0x47/64、0x1D0、0x1D4；长度含 NUL | 同一张表，C 侧绑定，长度 = size+1 | `.asciz`/64 B 默认值；长度 = `selinux_ctx_end - selinux_ctx`（含 NUL） | 无 | A `embed/libcxx_blob.hpp:34-42`、`K:208-247`；B `steps/hook_patch.cpp:229-240`、`:302-316`；C `libcxx.S:14-25`、`:144-151` |
| 19 | 分支编码（from/to/delta） | site→`payload_vaddr+0x90`；回跳 `payload_vaddr+0x1D4 → site+4` | 同构（`encode_branch(target.hook_vaddr, payload_vaddr+entry)`） | 同构（`BRANCH\|\|((shell+start-hook)>>2)`，回跳 `(hook+4)-(shell+len-4)`） | 无 | A `hook/sentry_hook.cpp:27-56`、`:109-127`；B `steps/hook_patch.cpp:122`、`:153`；C `exp.c:687-695` |
| 20 | guard/displaced 校验强度 | 写前读站点块并**两字都比对** | 只定位并保存，不比对 | 只判 `first_insn == hook_insn` 的幂等 | 无 | A `steps/pagecache_steps.cpp:147-154`；B `steps/hook_patch.cpp:41-60`；C `exp.c:691-693` |
| 21 | 写前读旧块的来源 | 同 fd 直读，失败才回退桥；splice 用 helper 或同 fd | 同构（直读→old_page 桥） | 同构（prefork pread / crash_dump 取页） | 无 | A `landing/real_splice_io.cpp:167-218`；B `pagecache/pagecache.cpp:116-166`；C `exp.c:157-208`、`:349-362` |

## §二 “高可能性”逐条展开

### H1（#1）站点块先于载荷落地

- 为什么可能 panic：`write_hook` 先把 libc++ 的站点字写成 `B payload_vaddr+0x90`（A `steps/pagecache_steps.cpp:155-164`），**之后**才写载荷（`:178-194`）。libc++.so 的页是 init 早已映射的同一物理页，页缓存一改，init 的下一次 `sentryC1` 调用**立刻**走分支：目标区如果还没有我们的字节（本次运行载荷尚未写成、或载荷写失败/被丢弃），init 在线程 1 上执行零字节/旧数据 → SIGILL/SIGSEGV；init 是 critical 进程，其死亡在 Android 上直接触发内核 panic/重启。B/C 的顺序都是从载荷到站点（B `steps/hook_patch.cpp:176-189`；C `exp.c:716-740`），正是为了不出现这个窗口。
- 触发条件：站点 pristine（guard/displaced 对得上）→ 写站点成功 → 在载荷 30 个块写完前，init 主线程打任意一条日志（或任何走到 `sentryC1` 的路径）。写载荷一块要一次 ESP 往返，窗口是几十毫秒级，不是纳秒级。
- 最小修复方向：顺序固定为“载荷全部写并逐块确认 → 再写站点块 → 立刻回读确认”；站点未确认前不得让分支生效；把“站点是否已改”做成显式状态，失败路径必须回写。

### H2（#2 + #5）钩子从不恢复，且 UMH 脚本把 mutex 删掉 → 可重入

- 为什么可能 panic：`/dev/df` 是载荷自己用 `O_CREAT|O_EXCL` 建的**一次性** mutex（C `libcxx.S:56-66`）；文件在 ⇒ 载荷直接 `return`（`:65-66`）。A 的脚本在 late-load 前后各删一次（`root_cmd.sh:69-72`、`:105`），等于**重新武装**仍留在 libc++ 里的分支（A 没有 restore，见 #2）。此后 init 每次在主线程打日志都会：建 /dev/df → clone → 写 `u:r:vendor_modprobe:s0` → `execve /vendor/bin/insmod <carrier>`（`libcxx.S:104-126`）。而 insmod 起来的正是 `ghostlock.ko`，它的 init 又会 UMH 跑同一个脚本、脚本又删 /dev/df（`ghostlock.c:139-166` + `root_cmd.sh:105`）——形成“加载→脚本→删 mutex→下一次 init 日志再加载”的自持环，每一轮还切换 SELinux permissive/enforcing 并 fork 一批 `ksud` 进程。B/C 都没有这个闭环（B 的脚本不删；C 的 App 只 stat）。
- 触发条件：钩子已装（#1 成功）且模块至少成功加载过一次；此后任何 init 主线程日志。
- 最小修复方向：脚本不碰 /dev/df；成功路径保留 mutex 作为“已点火”的持久证据；若确需二次点火，必须先在受控路径 restore 钩子，再显式删 mutex，再重新 apply。

### H3（#3）验证失败就重发 + 无回滚

- 为什么可能 panic：`write_image_verified` 在回读不匹配时**再次调用 write**（`steps/pagecache_steps.cpp:303-311`），200 ms 预算、2 ms 间隔（`K:495`、`steps:369`）意味着单块最多约百次 16 B 覆盖写；而 A 自己记录过“读视图与内存视图相差 8 字节”（`K:146-150`）——一旦坐标真是偏的，这百次写就是把 libc++ 代码页上**相邻指令**反复改坏，init 取指即崩。B 只读一次、失败即 journal 回滚（`steps/chain.cpp:252-266`、`:136-153`）；C 根本不回读（`exp.c:345-376`），因此也不会把一次意图放大成百次写。
- 触发条件：任一原因导致回读不匹配（ICV 被丢、页未落、坐标偏移、序号被防重放丢弃）。
- 最小修复方向：先“只读确认”，确认目标块**未被改动**才允许重发；重试上限 1–2 次；失败即用写前保存的原块回写（至少对钩子站点与载荷首块），并在 refusal 里保留“已写块数”。

### H4（#4）vma→file 换算被硬编码，且两处各算一次

- 为什么可能 panic：A 的 plan 是常量（`hook/sentry_hook.cpp:82-92`），Kotlin 又独立假定 `vaddr = file + 0x4000`（`K:236-238`）。站点两字（`bti c` / `ldr x8,[x1]`）能对上**并不证明** delta 是 0x4000：分支目标是按 vaddr 算的（`sentry_hook.cpp:109-127`），delta 一旦不同，分支会落到 landing 之外的页内数据/空白，init 执行即崩。B 的 `payload_file` 与 `payload_vaddr` 从同一 `exec_segment` 导出（`steps/elf_hook.cpp:412-414`），不存在第二个假设；C 由 `find_hook_target` 现场算（`exp.c:681-682`）。
- 触发条件：设备上的 libc++.so 与常量来源不是同一份（OTA、不同 build、或 payload 落点不是 exec LOAD 的 file end）。
- 最小修复方向：vaddr↔file 只保留**一处**推导（从 program headers），branch 与 jump-back 都用这一份；plan 只作为“期望值”，与推导值不一致即拒绝，而不是直接写。

## §三 “我们新增但 B / C 都没有”的清单

1. **`rm -f /dev/df`（late-load 前）**：`tools/lkm/ghostlock/root_cmd.sh:69-72`。B 同文件 `:59-70` 无此动作；C 只有 `libcxx.S:56-66` 的 O_EXCL 建文件，无删除者。
2. **`rm -f /dev/df`（成功路径）**：`root_cmd.sh:105`。同上，B/C 均无。
3. **`ksud post-fs-data`**：`root_cmd.sh:87-89`；**`ksud services`（setsid 后台常驻）**：`:90-92`；**`ksud boot-completed`**：`:95-97`。B 的脚本只跑 `late-load`（`:55-57`）；C 的 C 侧命令串只有 `<ksud> && touch …`（`dirtyfrag.c:58-60`）。
4. **root 域里对 /dev 的写**：`chmod 666 /dev/glk` + `chcon u:object_r:null_device:s0 /dev/glk`（`root_cmd.sh:24-25`）；`touch /dev/dfm0|dfm1` 之外还写 `/data/local/tmp/.ghostlock_lkm_ok|_fail`（`:40,46,52,60,106,112`）。B 只有 dfm0/dfm1（`:29,35,41,49,63,69`），C 只有 dfm0/dfm1（`dirtyfrag.c:59`）。
5. **钩子写序**：站点→载荷（`steps/pagecache_steps.cpp:155-194`）；B/C 都是载荷→站点。
6. **无 restore**：全树唯一的“恢复”是 `.gl_restore` 存在时写回硬编码的 16 字节（`P:1278-1291`），不是状态化的 restore。
7. **验证失败重发**：`steps/pagecache_steps.cpp:303-311`；B 无重发，C 无回读。
8. **触发前二次应用站点**：`P:1366-1384`；B/C 只 apply 一次。
9. **自选引爆者**：`crash_dump32 1 1 11 1`（`P:1409-1415`）；C 依赖 init 回收孤儿（`exp.c:763-773`）。
10. **硬编码 plan**：`hook/sentry_hook.cpp:63-107` + `K:236-238` 的 `+0x4000`；B 运行时推导（`elf_hook.cpp:341-414`）。
11. **槽位填充搬到 Kotlin**：`K:176-249`；B 在 C 侧 `make_libcxx_hook_bindings`（`hook_patch.cpp:274-323`），C 是静态 `.S`。
12. **helper argv 的头注释与代码不符**：`embed/splicehelper_blob.hpp:9-11` 写 `argv[3]="r"`，而 `landing/crash_dump_bridge.cpp:116` 只传 `{offset,path}`；后者等价于 C 的两参 splice 形态（`exp.c:185`、`254`），功能正确、只有注释错。

## §四 结论

- **panic 最可能来自 #1（站点先于载荷）+ #2（无 restore）**，两者叠加后被 #5（脚本删 `/dev/df`）变成可重复触发：先是一次“分支指向未写区”的 init 执行，后是一轮轮“init 日志 → insmod → UMH 脚本 → 删 mutex”的自持环。B 用“载荷先写 + 永不删 mutex + 终点无条件 restore”三件事同时堵住了这三条；A 三件都没有。
- 次要但独立的高风险是 #3（验证失败重发且无回滚）与 #4（vaddr 假设）；它们不需要 init 配合，只要页写不中就会把代码页反复改坏。
- **下一步先在离线验证**（不碰设备、不跑 gradle）：

  1. 用 ELF 推导（B 的 `steps/elf_hook.cpp` 同一套公式）在 host 上核对：`0xFE628` 是否等于 exec LOAD 的 `file_offset+size`、delta 是否真为 `0x4000`、room 是否 ≥ 480、站点前 4 字节是否 `0xD503245F`、站点是否 `0xF9400028`。
  2. 用 host 单测 + 假 surface 记录写序列，断言 `write_hook` 的写序应为“载荷块…→站点块”，并断言任一失败路径都会回写站点；当前实现会在这条断言上失败。
  3. 用 host 的帧/HMAC 向量对拍 A/B/C 的 40 字节 MAC 输入（A 与 B 已逐字相同，可作为回归基线）。
  4. 静态对拍 B 的 `steps/chain.cpp:136-153` 回滚与 `:279-284` restore，列出 A 缺失的状态位（hook_applied / journal_count / rollback_incomplete）。
