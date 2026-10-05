# 上游 DirtyFrag（ankitrawatgit）patch / 触发 / 回滚处理对照

> 范围：只读分析上游处理方式，并与本项目 GhostLock 的 CVE-2026-43284 分阶段入口对照。
> 本文只描述事实与差异，不修改任何源码，不构成对真机行为的保证。

> **状态更新（2026-10-05）**：本仓库**已删除** vendored 的 `DirtyFrag-Android-Root-Jailbreak@de2ab7b ` 目录（理由：不参与构建；
> 上游 repo 可直接查阅，来源与 commit 记入 `README.md`/`README_ZH.md` 的「Credits & License」）。
> 本文中 `DirtyFrag-Android-Root-Jailbreak@de2ab7b ...` 的旧引用已改写为 `上游仓库@commit <path>`；文中「逐字节一致」的结论
> 是当时 vendored 状态下得出的，仍然有效，但现在需按上游 commit 自行 clone 复核。

## 0. 资料与引用约定

- 上游 clone：`/tmp/upstream/ankitrawatgit_DirtyFrag-Android-Root-Jailbreak`，HEAD
  `de2ab7be69dc159af508d584523fd4d5c0b7cc7a`（2026-10-04 +0530，remote
  `ankitrawatgit/DirtyFrag-Android-Root-Jailbreak`）。
- 本文行号引用格式：`上游 <path>:<line>`。本仓库 `DirtyFrag-Android-Root-Jailbreak@de2ab7b usermode/ankit/`
  与上游 `app/src/main/jni/` 的 `exp.c`、`libcxx.S`、`elf_parser.c`、`include.inc`、
  `CMakeLists.txt` **逐字节一致**（`splicehelper.c` 不在 ankit 目录，见下）；
  `DirtyFrag-Android-Root-Jailbreak@de2ab7b app-reference/ExploitRunner.java` 与上游一致；
  `DirtyFrag-Android-Root-Jailbreak@de2ab7b lkm/ankit/dirtyfrag.c` 与上游 `dirtyfrag-lkm/dirtyfrag.c` 一致。
- 项目侧引用格式：`<path>:<line>`（仓库根相对）。
- **未验证**标记：无上游代码/机制直接支撑、只能真机或内核行为确认的点。

## 1. 结论要点（TL;DR）

1. 上游一共改 3 处**页缓存**：patch #1 `crash_dump64`←splicehelper、patch #2 vendor `.so`←`.ko`、
   patch_hook `/system/lib64/libc++.so` 的 libc++ sentry 符号；**本仓库上游只有 libc++.so 一个 hook，
   没有 libbase/其它库**。
2. 上游只对 **libc++.so 的 shellcode(tramp) 保存并恢复原字节**（`restore_hook`）；`crash_dump64` 用
   `fadvise(DONTNEED)` 丢缓存而非写回，vendor `.so` **完全不恢复**。写不落盘、重启即恢复是**机制推断**，
   上游没有显式清 page dirty，未验证。
3. patch #2 的载体只能用「写前读旧值失败」的 `-2` 触发换目标：**已写过一个字节就绝不再换**；
   默认 4 个 vendor 候选 + 用户配置（≤16 条），仅 Java 侧做路径前缀/长度校验。
4. 触发链是「double-fork 让孙进程被 init 收养 → init 打日志命中 libc++ hook → shellcode 写
   `attr/exec=vendor_modprobe`、exec `/vendor/bin/insmod <ko>` → LKM init 用 UMH 跑 ksud 脚本 →
   `ksud late-load --kmi`」，patch #1/#2/#hook 缺一不可。
5. 本项目分阶段入口目前只覆盖上游 patch #2（把 `.ko` 写进单个 carrier）与 double-fork 触发/终态探测：
   **没有内嵌 splicehelper（patch #1）、没有 crash_dump 域切换桥、没有 libc++ hook 应用与恢复**，
   `trigger/full` 真机跑通**前置必须补齐**这些。
6. `--allow-dev-target` 只是放宽 carrier 路径前缀（上游无此开关，上游恒定要求
   `/vendor/` 或 `/system/vendor/`），它**不能替代** missing 的 patch #1/#hook/桥。

## 2. Patch 目标清单

### 2.1 patch #1：`/apex/com.android.runtime/bin/crash_dump64` ← splicehelper

| 项 | 内容 | 证据 |
|---|---|---|
| 目标路径 | 硬编码 `/apex/com.android.runtime/bin/crash_dump64` | `上游 app/src/main/jni/exp.c:55`（`kCrashDump`） |
| 写入偏移 | `0`（文件头） | `exp.c:621`（`patch_file_cbc(kCrashDump, sh_buf, sh_len_padded, 0, 0, ...)`） |
| 内容来源 | `splicehelper` 可执行文件，编译自 `splicehelper.c`（`-nodefaultlibs -nostartfiles -ffreestanding -static`，再 `strip`），用内联汇编 `.incbin "splicehelper"` 嵌入 `exp` | `上游 app/src/main/jni/CMakeLists.txt:8-21`；`exp.c:416-420` |
| 长度 | `pad16()` 向上取整到 16 的倍数 | `exp.c:487-493`、`615-619` |
| 语义 | 让「exec `crash_dump64`」跑到注入的 helper，借助 crash_dump 的 SELinux 域（exec 过渡按路径标签）去 open/读 vendor 文件 | `exp.c:612-614` 注释；`splicehelper.c:47-88` |

注意：`splicehelper.c` 的 argv 约定是 `argv[1]=offset, argv[2]=path, argv[3]="r"`（可选读模式）；
但本版 `exp.c` 两处调用都**只传 offset+path（splice 模式）**，`"r"` 读模式实际未被使用
（`read_vendor_content` 也走 splice 模式，把 16B 页塞进 stdout 管道）。
证据：`exp.c:170-186`、`exp.c:247-256`；`splicehelper.c:51-88`。

### 2.2 patch #2：vendor `.so` ← `dirtyfrag.ko`

| 项 | 内容 | 证据 |
|---|---|---|
| 默认候选 | 4 个：`/vendor/lib64/libbinderdebug.so`、`libstagefrighthw.so`、`libstagefright_aidl_bufferpool2.so`、`libbsp_module.so`（顺序即优先级） | `上游 app/src/main/java/df/root/ExploitRunner.java:26-31` |
| 用户可配 | SharedPreferences `dfroot.vendor_targets`，每行一个绝对路径；Java 解析去重、非法项丢弃；`normalize/validate` | `ExploitRunner.java:130-193`、`MainActivity.java:357-381` |
| 配置上限 | Java `MAX_VENDOR_TARGETS = 16`；native `MAX_VENDOR_TARGETS 16` | `ExploitRunner.java:32`；`exp.c:58-61` |
| native 注入 | Java 把首选项 + 列表字符串传进 `nativeRunAll`，`configure_vendor_targets()` 用 CR/LF/逗号 分词、trim、去重；首项始终是 Java `detectKoTarget` 选中的路径 | `exp.c:99-138`、nativeRunAll `exp.c:815` |
| 写入偏移 | `0` | `exp.c:666`（`patch_file_cbc(libcxx_ko_target, ko_buf, ko_len_padded, 0, 1, ...)`，`use_helper=1`） |
| 内容来源 | ① 内置：`select_ko_image()` 从 8 个内嵌 `.ko`（`.incbin`）按 (androidN, major.minor) 选；② 自定义：`read_custom_ko()` 读文件并校验 | `exp.c:396-414`、`434-473`、`504-565`、`568-610` |
| 自定义 `.ko` 校验 | 常规文件、20B–64MiB、ELF64 LE AArch64、含 `name=dirtyfrag`、`description=DFRoot LKM`、`vermagic=<major.minor>.` 前缀 | `exp.c:513-561` |
| 目标路径长度 | shellcode 64 字节缓冲；native 校验 `len>0 && len<64` | `exp.c:72-84`；`libcxx.S:23-30` |

**重试语义（关键）**：`patch_ko` 对候选表顺序循环，仅当 `patch_file_cbc` 返回 `-2` 才尝试下一个；
返回 `0` 结束、其它非 0 立即结束。`-2` == 「该目标第 0 块读旧内容就失败，尚未写入任何字节」，即
「写前才可换目标」。证据：`exp.c:655-671`、`exp.c:295-382`（见 `3.3）。

### 2.3 patch_hook：`/system/lib64/libc++.so`

| 项 | 内容 | 证据 |
|---|---|---|
| 目标库 | 仅 `/system/lib64/libc++.so`；本仓库**无 libbase/其它 hook** | `exp.c:852-855`；对上游 jni/java 搜 libbase 无源码命中 |
| 符号 | `_ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE6sentryC1ERS3_`（`std::__1::basic_ostream<char>::sentry::sentry(basic_ostream<char>&)` 的 C1） | `exp.c:853` |
| hook 偏移 | ELF `.dynsym` 解析 `st_value`，换算 `st_value - executable_vaddr + executable_off`；命中 PACIASP/PACIBSP/BTI 则 +4 再取一条 | `elf_parser.c:94-115`、`132-138` |
| shellcode 偏移 | 第一个可执行 `PT_LOAD` 的末尾：`p_offset + p_filesz` | `elf_parser.c:46-51`（`*payload_target`） |
| shellcode 内容 | `libcxx.S` 的 `.data` 段：路径字符串（`/dev/df`、`u:r:vendor_modprobe:s0`、`/proc/self/attr/exec`、`/vendor/bin/insmod`、64B `ko_target`）+ 代码（uid==0 && tid==1 判定、mutex、clone、写 attr/exec、execve insmod） | `libcxx.S:12-34`、`44-133` |
| 参数化 | 调用方 `patch_hook()` 把 branch 跳到 `shell_off`，把 `jmpback` 写进 shellcode 最后一字，把原首指令写进 `first_inst_copy` | `exp.c:687-699`；`libcxx.S:144-147` |
| 写入顺序 | 先写 shellcode（整个 blob，可多块），再写 4 字节 hook branch 所在的 16B 对齐块 | `exp.c:716-741` |

`ko_target` 在 native 里位于 `libcxx_data` 的固定偏移（`libcxx_ko_target_off`），运行期被
`strncpy(..., 63)` 覆写为 patch #2 实际使用的路径（`exp.c:805-811`、`662-663`）。因此
**patch #2 选中的载体 = shellcode 之后 insmod 的模块文件，二者必须一致**。

## 3. 回滚 / 恢复

### 3.1 保存原字节并恢复的只有 libc++.so

- `patch_hook()` 传入 `struct PatchRestore *restore`：
  - 写 shellcode 前 `pread()` 保存 `shell_off` 起 `padded` 字节到 `shell_orig`（堆）；
  - 写 trampoline 前 `pread()` 保存 `hook_off & ~15` 的 16B 块到 `tramp_orig[16]`，置 `valid=1`。
  证据：`exp.c:702-714`、`721-734`。
- 若命中时 `first_insn == hook_insn`（已经 hook 过），`patch_hook` 提前返回 0，**不设置 restore**
  （`exp.c:691-693`）。
- `restore_hook()` 用同一条 `patch_file_cbc` 先写回 `tramp_orig`（16B @ 对齐块），再写回
  `shell_orig`（padded @ `shell_off`）。`nativeRunAll` 的 `done:` 无条件调用它，之后
  `free(shell_orig)`。证据：`exp.c:745-753`、`887-893`。

### 3.2 不恢复的两处

- **patch #1（crash_dump64）**：没有保存原字节，也没有字节级恢复；只用
  `fadvise_drop()` 对目标做 `POSIX_FADV_DONTNEED` 丢页缓存（best-effort）。
  证据：`exp.c:755-761`、`891`。
- **patch #2（vendor `.so` 里的 `.ko`）**：既不保存也不恢复，也不 fadvise；页缓存里一直是 `.ko`
  内容，直到自然回读淘汰或重启。证据：`exp.c:655-674`（无 restore 调用）。

### 3.3 `patch_file_cbc` 的 `-2` 语义与「写前才可换目标」

- 返回值：每块先读 `old_content`（系统文件 `pread`；vendor 用 crash_dump 桥），再算 IV、发 1 个 ESP 包。
  - 第 `i==0` 块读旧值失败 → `rc = -2`（**一个字节都还没写**，调用方可换目标）；
  - 第 `i>0` 块读旧值失败 → `rc = -1`（已经写过一部分，不可换）；
  - 某个 ESP 写失败 → `rc = -1`。
  证据：`exp.c:344-376`（`rc = i == 0 ? -2 : -1`）。
- `patch_ko` 循环：`ret==0` break；`ret!=-2` break（视为最终失败）；`ret==-2` 打印并试下一个候选。
  证据：`exp.c:658-671`。
- 同源镜像：项目 `src/core/backend/cve_2026_43284/steps/chain.cpp` 的 `apply_plan` 只在「本 carrier
  第一次读旧块失败且尚未写」时返回 `CarrierUnusable`，`run_chain` 据此换下一个 carrier，并
  `clear_journal`；写失败/校验失败则直接 `finish` 终结，不再换目标。证据：`chain.cpp:201-208`、
  `340-348`。

## 4. 持久性判断（分区 / 页缓存 / 是否依赖重启恢复）

- 三个目标分别在 `/apex`、`/system`、`/vendor` 分区；Android 上通常是 dm-verity（或 apex 镜像）
  只读挂载。上游**没有写块设备**：原语是把目标页 `splice` 进 pipe，再 `splice` 到 IPsec UDP socket，
  由内核 ESP 路径在**共享的页缓存页**上就地变换。证据：`exp.c:240-282`（vmsplice 头/IV +
  `splice(file_fd -> pipe)` + `splice(pipe -> sk_send)`）。
- patch #1 的 verify（读 `crash_dump64` offset 16 与注入内容比对）本身就是「改动只在页缓存可被
  后续读取看到」的证据；失败提示词面写的是 "crash_dump64 page cache unchanged"。
  证据：`exp.c:625-648`。
- 清理手段也指向「页缓存可丢弃、重启即恢复」：只有 `restore_hook()`（字节恢复）+ `fadvise(DONTNEED)`
  （丢缓存），没有 fsync/flush/块设备写。证据：`exp.c:755-761`、`890-891`。
- 因此**推测**：修改不落盘，重启后页缓存失效、从 verity 块重新读取 → 恢复原始内容。
- **未验证**：
  1. 上游代码未显式处理 page dirty / writeback 语义；ESP 就地写是否会把页标脏、进而在回写时
     落到块设备（verity 是否拦截/报错）——未验证。
  2. 只读挂载 + dm-verity 下 `POSIX_FADV_DONTNEED` 是否真能丢弃仍被本进程/其它进程映射的页——
     未验证。
  3. 项目设计文档已给出同样推断（`docs/analysis/cve-2026-43284-b5-design.md:142-147`：
     「写不落盘…重启即恢复」），但属设计推断，不是设备证据。

## 5. 触发流程（patch → crash_dump → LKM → ksud）

### 5.1 顺序与失败处理

1. `nativeRunAll` 先 patch：`patch_ko()`（patch #1 → patch #1 verify → patch #2 候选循环），
   成功后 `patch_hook("/system/lib64/libc++.so", ...)`。任一失败 `goto done`（rc=3）。
   证据：`exp.c:844-855`、`887-888`。
2. `usleep(500ms)` 后 `createOrphanProcess()`：`fork` 中间子进程，子进程再 `fork` 孙进程；
   子进程立即 `_exit(0)`，父进程 `waitpid` 中间子进程；孙进程 `sleep(1)` 后 `_exit(0)`，成为
   被 init 收养的孤儿。证据：`exp.c:763-773`、`858-860`。
3. 孙进程退出 → init 收尸/打日志，走到 libc++ sentry 构造 → 命中 patch_hook 的 branch →
   执行注入 shellcode。shellcode 先要求 `getuid()==0` 且 `gettid()==1`（只在 init 主线程生效），
   用 `openat("/dev/df", O_CREAT|O_EXCL|O_CLOEXEC)` 做一次性互斥；已存在就直接返回。
   证据：`libcxx.S:44-66`。
4. shellcode `clone()` 出 worker；worker 再 `clone()` 出孙进程。孙进程写
   `/proc/self/attr/exec = "u:r:vendor_modprobe:s0"`，再
   `execve("/vendor/bin/insmod", ["/vendor/bin/insmod", ko_target, NULL])`。
   证据：`libcxx.S:78-128`。
5. `insmod` 对 `ko_target`（patch #2 的 vendor `.so`，内容已被换成 `.ko`）走模块装载
   （内核 `init_module`/`finit_module`，`include.inc` 里两处 syscall 号备查）。
   证据：`include.inc:165,168`。
6. LKM `dirtyfrag_init`：kprobe 取 `kallsyms_lookup_name` → 把 `selinux_state` 首字节置 false
   （permissive）→ 取 `call_usermodehelper_setup/exec` → 组装
   `cmd = "/data/user_de/0/df.root/ksud && touch /dev/dfm0 || touch /dev/dfm1"`，
   `argv = {/system/bin/sh, -c, cmd}`，`si->path = "/system/bin/sh"`；对 `task_defex_user_exec`、
   `get_dc_target_dpath` 挂 pre_handler，把返回值置 0、`pc = lr` 以旁路 Defex；
   `umh_exec(info, UMH_WAIT_PROC)`；最后 `return -E2BIG` 让模块自身装载失败/卸载
   （源码注释：没有 `module_exit`，不长期驻留）。
   证据：`上游 dirtyfrag-lkm/dirtyfrag.c:16-88`（镜像 `DirtyFrag-Android-Root-Jailbreak@de2ab7b lkm/ankit/dirtyfrag.c`）。
7. `ksud` asset 脚本：自定义 `ksud.custom`（可执行）优先，否则查 KernelSU Manager 的
   `libksud.so`，再 ReSukiSU，再 `/data/adb/ksu/bin/ksud`；`chmod` 后可执行；
   `exec "$KSUD" late-load --kmi`（KMI 取 uname 的 androidN + major.minor）。
   证据：`上游 app/src/main/assets/ksud:1-34`；staging 目标目录来自
   `ExploitRunner.stageKsud()`（device-protected app data = `/data/user_de/0/df.root/`）。
   证据：`ExploitRunner.java:195-217`。
8. app 侧等待：轮询 `/dev/df`、`/dev/dfm0`、`/dev/dfm1` 共 5000ms（10ms 步进）。
   `dfm0`→rc0 成功、`dfm1`→rc1 ksud 失败、`/dev/df`→仅日志继续；超时 rc=2「check logs」。
   证据：`exp.c:862-886`。
9. 失败处理：patch 失败 rc=3；触发/等待失败 rc=2；`done:` 里统一 `restore_hook` + `fadvise_drop`。
   证据：`exp.c:887-893`。

### 5.2 patch/触发时序图

~~~mermaid
sequenceDiagram
    autonumber
    participant A as App(native exp)
    participant K as Kernel(XFRM/ESP)
    participant CD as crash_dump64(域 crash_dump)
    participant INIT as init(uid0,tid1)
    participant LK as libc++ hook(shellcode)
    participant KO as dirtyfrag.ko(LKM)
    participant KSUD as ksud late-load

    Note over A: IpSecManager 建 SA(AES-CBC/HMAC)<br/>openUdpEncapsulationSocket + allocateSPI
    A->>K: patch #1: splicehelper→crash_dump64 页缓存@0
    A->>CD: exec crash_dump64 [off,path](splice 模式)
    CD->>K: splice vendor 页→pipe/ESP 写
    A->>K: patch #2: dirtyfrag.ko→vendor .so@0<br/>(读旧值首块失败=-2 才换下一候选)
    A->>K: patch_hook: shellcode→exec PT_LOAD 尾<br/>+ branch→libc++.so sentry
    Note over A: 保存 shell_orig / tramp_orig
    A->>A: createOrphanProcess(double-fork)
    A->>K: usleep(500ms)
    Note over A: 中间子退出,孙进程被 init 收养
    INIT->>INIT: 孙进程退出→收尸/打日志
    INIT->>LK: libc++ sentry 命中 hook→exec shellcode
    LK->>LK: uid0 && tid1 判定, /dev/df 互斥
    LK->>LK: clone worker→写 attr/exec=vendor_modprobe
    LK->>KO: execve /vendor/bin/insmod <ko_target>
    KO->>KO: selinux permissive + Defex 旁路
    KO->>KSUD: UMH: /system/bin/sh -c "/data/user_de/0/df.root/ksud && touch /dev/dfm0 || touch /dev/dfm1"
    KSUD->>KSUD: exec ksud late-load --kmi
    KSUD-->>A: /dev/dfm0(成功) / /dev/dfm1(ksud 失败)
    A->>A: 轮询 marker(5000ms) → rc
    A->>K: cleanup: restore_hook + fadvise_drop(crash_dump64)
~~~

## 6. 安全 / 校验

| 校验点 | 上游行为 | 证据 |
|---|---|---|
| KMI 不匹配拒绝 | 若用户指定了 KMI，先 `sscanf("android%d-%d.%d")`；解析失败或 major/minor 不符 → `NULL`；否则在 8 项表里精确匹配 (androidN, major.minor)，找不到 → `NULL`；`patch_ko` 打印 "does not match running KMI ...; refusing to patch" 并返回 1。**绝不跨 android release 猜测** | `exp.c:446-459`、`591-605`（主题行 ~595） |
| 无 KMI 后缀 | `uname -r` 无 `androidN` 时 `andr<=0`，不做自动选择；除非用户显式给了完整 KMI | `exp.c:462-473`；`read_device_versions` `475-482` |
| 自定义 `.ko` 预检 | ELF64 LE AArch64 + `name=dirtyfrag` + `description=DFRoot LKM` + 匹配 `vermagic=<major.minor>.` | `exp.c:544-561` |
| 目标路径校验（native） | 必须 `/vendor/` 或 `/system/vendor/` 前缀；`len>0 && len<64`；不含空格/制表/换行/回车 | `exp.c:72-84` |
| 目标路径校验（Java） | 同上 + `<=63` + 无 NUL/空格/tab；去重；≤16 条 | `ExploitRunner.java:141-193` |
| Java KMI 选择一致性 | 下拉选中与运行内核 major.minor 不一致则拒绝/回退 auto；`sameKernelVersion` 以 major.minor 比较 | `MainActivity.java:320`、`499-503` |
| patch #1 verify | `pread(crash_dump64, 16, offset=16)`，与 `splice_helper_start+16` 比对；能读且不等则 FAILED 返回 -1；读不到则 "verify skipped" 继续 | `exp.c:625-648` |
| patch #2 verify | **没有**写后回读校验；只有「读旧值失败 → 不写 / 换目标」。`patch_file_cbc` 不会读回写入结果 | `exp.c:344-376`、`655-674` |
| patch_hook verify | 无回读校验；仅 `first_insn==hook_insn` 的幂等早退 | `exp.c:691-693` |
| 失败重试次数 | patch #1：无重试。patch #2：每个候选最多一次，候选上限 16；只有 `-2` 才继续。ESP 写失败无重试 | `exp.c:58`、`658-671` |
| 运行前门禁（Java） | `Run` 按钮在已存在 `/dev/df` 时禁用（一次运行后避免重复） | `MainActivity.java:505-511` |

## 7. 与本项目分阶段入口对照

对照对象：`--run-cve-2026-43284 <module> <target> --stage=plan|write|trigger|full [--allow-dev-target]`
（`src/core/backend/cve_2026_43284/stage_runner.*` + `real_ops.*` + `steps/chain.*`）。

| 上游环节 | 上游 | 本项目现状 | 缺口判定 |
|---|---|---|---|
| patch #1 splicehelper 内嵌 | `.incbin "splicehelper"`，写 crash_dump64@0 | **无**任何 incbin/splicehelper 嵌入或写入；仅注释引用 | **缺** |
| crash_dump 域切换桥（vendor 读写） | `read_vendor_content`/`do_one_write_cbc` 用 raw `clone(CLONE_VFORK/CLONE_VM)` + `execl(crash_dump64,[off,path])` | `real_chain_read_block` 只有对 `page.file_fd` 的 `pread`；`pagecache` 无 helper 路径 | **缺** |
| patch #2 载体与 `.ko` | 4 默认 + 用户配置 + 逐候选 `-2` 回退；`.ko` 从 8 内置/自定义 | `plan` 把传入 `.ko` 读到内存、零填充，单 region @ target+0；`run_chain` 支持 carrier 回退，但 staged CLI **只建 1 个 carrier**（`request.carrier_count=1`） | 单次写/校验已具备；**旧值读取对 vendor 走不通**（无桥），默认候选/用户配置未接入 staged |
| patch_hook（libc++） | 定位 sentry 符号 → 写 shellcode@PT_LOAD 尾 + branch@hook | `steps/elf_hook.*` + `steps/shellcode.*` 只**计算描述**（`HookPlan`），**没有任何调用方把它接到 chain** | **缺**（纯 host 计算，未应用） |
| shellcode 参数化/触发形态 | 固定 `libcxx.S`，运行期改 `ko_target`、写 branch/jmpback | `build_shellcode`/`build_hook_plan` 只在测试里使用 | **缺** |
| 恢复 | `restore_hook`（tramp+shell）+ `fadvise_drop(crash_dump)` | chain 对**写入的模块 region** 做 journal 回滚 + 回读校验（`steps/chain.cpp`）；没有对 crash_dump/libc++ 的恢复，因为没有写它们 | 回滚语义比上游更严（覆盖模块 region），但**未覆盖 hook/桥** |
| trigger | double-fork 孤儿 sentry | `real_chain_trigger` 同构 double-fork（可额外 exec_path） | 形态一致；**因无 hook，触发在真机上不会加载任何东西** |
| wait | `/dev/df`、`/dev/dfm0`、`/dev/dfm1` | `/dev/dfm0`、`/dev/dfm1`、`/sys/module/dirtyfrag`（**无 `/dev/df`**） | `/dev/df` 由 shellcode 创建，本项目无 shellcode 所以无法创建；`/sys/module/dirtyfrag` 存疑（LKM `return -E2BIG` 自卸载） |
| KMI/vermagic 预检 | `select_ko_image` / `read_custom_ko` | `lkm/lkm_policy.*` + `lkm/lkm_image.*` host 预检（8 KMI、vermagic、空 `__versions`、未签名），但 staged 入口未强制走预检（`build_module_plan` 只查大小） | 策略已具备、**未接线** |
| session/IPsec | Java `IpSecManager` 建 SA；native 组装 ESP | 已具备：84B 通道 B 帧 + `connect_esp_socket` + `make_real_chain_ops` | 已具备 |
| `--allow-dev-target` | 上游无此开关；恒定要求 vendor 前缀 | 仅 staged 入口放宽 carrier 路径前缀，记录 `run.dev_target allow=1` | 项目**新增**的调试口，不能替代 missing 环节 |

补充事实：
- 项目 `plan` 只校验并打印一个「模块@target+0」的 region（`stage_runner.cpp:135-189`、`198-204`）。
- 项目 `write` 会写并逐块回读校验，但 carrier 的旧值读取是 `pread`（`real_ops.cpp:95-102`），
  vendor `.so` 在 app 域不可 open 时直接 `CarrierUnusable` → 因此真机只有 `--allow-dev-target`
  指向可读文件时 write 才能过（B5-9d 门禁即 `/data/local/tmp/target.bin`）。
- B5-9d 真机证据只到 write：`docs/analysis/device-gates/B5-9d-write-20261003-pagecache-primitive-pass.md`；
  文档明确「未加载模块、未 fork/exec 目标；完整提权判定仍需冷机 + trigger/full」。

## 8. 真机 `trigger` / `full` 前必须补齐 / 确认的项

**必须补齐（代码层面，按上游顺序）**

1. patch #1：构建/内嵌 `splicehelper`，写 `/apex/com.android.runtime/bin/crash_dump64@0`，
   并实现 offset 16 的 pread verify（`exp.c:625-648`）。没有它，crash_dump 域里没有可执行 helper。
2. patch_hook：把 `steps/elf_hook` + `steps/shellcode` 的 `HookPlan` 真正接到 chain：
   写 shellcode@`PT_LOAD(p_offset+p_filesz)`、写 branch@sentry 符号偏移、写回末字 `jmpback`、
   把原首指令塞进 `first_inst_copy`（`exp.c:687-741`）。
3. 恢复：实现 `restore_hook` 等价物（shell_orig + tramp_orig）与 `fadvise_drop(crash_dump)`，
   并在所有退出路径执行（`exp.c:745-761`、`890-893`）。
4. crash_dump 桥：实现 `read_vendor_content`/写路径的 `clone+execl(crash_dump64)`，
   否则 vendor 载体读旧值失败 → 只能永远依赖 `--allow-dev-target` 的非 vendor 文件，
   而 shellcode 又会去 insmod 一个 vendor 路径，链路不闭合。
5. patch #2 的候选/用户配置/`-2` 回退接入 staged（当前 `carrier_count=1`）；默认 4 候选来自
   `ExploitRunner.java:26-31` / `chain.hpp:70-75`。
6. 把 `lkm/lkm_image` 预检接到 `build_module_plan`（staged 现在对任意 `.ko` 只查大小，
   `stage_runner.cpp:155-160`）。
7. 触发与终态 marker 对齐：要么由 shellcode 创建 `/dev/df`，要么在 wait 里去掉对它的依赖；
   并确认 `/sys/module/dirtyfrag` 是否可作为成功判据（`dirtyfrag.c:84` 返回 `-E2BIG`）。

**必须真机确认（无法 host 验证）**

8. patch #1 后 `exec crash_dump64` 是否进入预期域、能否 open 目标 vendor `.so`（固件差异大）。
9. vendor 载体是否同时满足：`vendor_file` label、可被 crash_dump 打开、大小足够容纳 `.ko`。
10. libc++ sentry 符号/指令序（BTI/PAC）在该设备成立；hook 是否真的被 init 调用（错 offset →
    init/libc++ 崩溃、启动循环风险）。
11. `insmod`/`finit_module` 对内核是否接受（vermagic/modversions/未签名/kCFI），
    以及 `/system/bin/sh` 写 `attr/exec=vendor_modprobe` 是否被 SELinux 允许。
12. Defex 符号 `task_defex_user_exec`、`get_dc_target_dpath` 是否存在、kprobe 是否成功、
    `selinux_state` 覆盖是否生效。
13. UMH 是否被 `CONFIG_STATIC_USERMODEHELPER` / 厂商策略拦截；`subprocess_info->path` 偏移是否成立。
14. ksud late-load 参数与目标 KernelSU 版本兼容，且要求冷机（KernelSU 未加载）。
15. page-cache 写是否真被 verity/文件系统策略阻断以及重启恢复（`4 的三个未验证点）。

## 9. 最大未验证项

1. **原语持久性/回写语义未验证**：上游没有显式清 page dirty，也没有 fsync/flush；「不落盘、重启即
   恢复」是从「页缓存写 + `fadvise(DONTNEED)`」推断的，不是设备证据。若内核把该页标脏并回写，
   dm-verity 的行为（拒绝、报错或静默）未知。
2. **触发链的成立性未验证**：patch #1 可执行、crash_dump 域可 open vendor 载体、libc++ sentry 被
   init 实际调用——三者任一不成立，`trigger`/`full` 都只会 double-fork 后超时。
3. **本项目与上游的差距是「缺 3 个 patch 中的 2 个」**：当前 staged chain 只做了 patch #2 的写与校验；
   在补齐 patch #1 + crash_dump 桥 + patch_hook + 恢复之前，真机 `trigger`/`full` 不具备成功条件，
   与 `--allow-dev-target` 无关。
