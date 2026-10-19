# 43284 动态 code cave（Kotlin-only）计划

- 日期：2026-10-19
- 级别：L（攻击关键路径）
- 关系：本件是 43284-production-engine-plan.md 的 P2/P3 攻击路径修订子计划；payload-finish-chain-plan.md 的收尾链保持不变
- 触发：模拟器固定 plan 的 landing 0xFE628 超出 libc++ 的 671496 B；hook_armed=1 但 hook=0
- 约束（用户裁定）：不在 C++ 加逻辑；探测、解析、code cave 搜索、plan 计算全部在 Kotlin；写入走现有 PageCacheAccess

## 1. 问题与目标

固定 plan 来自参考设备 libc++：

```text
site_file   = 0xB30AC
landing     = 0xFE628
payload_max = 0x9D8
```

模拟器 libc++ 只有 671496 B，site 与 landing 都越界；payload 写入 0 块，hook 不成立。

目标：

```text
Kotlin 读目标 ELF
  → 解析 PT_LOAD / .text / .dynsym / .dynstr / .rela
  → 定位 sentry 符号与 guard
  → 搜索可执行 code cave
  → 计算 site / landing / branch / jump-back / max
  → 通过 PageCacheAccess.write 逐块写入
  → Kotlin 直接读回校验
```

非目标：不重写 ESP 原语；不新增 C++/JNI 逻辑；不改变收尾链；pipe-splice 先只做可行性探测。

## 2. 第 0 步：回退未批准改动

```text
删除 core/src/backend/cve_2026_43284/hook/elf_hook.hpp
删除 core/src/backend/cve_2026_43284/hook/elf_hook.cpp
移除 core/CMakeLists.txt 中 elf_hook.cpp 一行
恢复 sentry_hook.cpp 的 device_sentry_plan() 为固定 plan
撤销 TypedBackendProbe 中 directMatches/directMatchesWord 的 hook/site 覆盖
保留 padToBlock（模块/载荷 16B 对齐的机械修复）
保留测试与 UMH 门禁
```

验收：nativeBuildHost/nativeBuildAndroid 绿；app 单测与 assembleDebug 绿；源码中无 elf_hook/plan: derived。

## 3. Kotlin 探测层

### 3.1 ElfTarget

用 RandomAccessFile 读取目标字节，校验 ELF64/little-endian/AArch64，上限 64 MiB；只读，不需要 JNI。失败返回具名原因（target-unreadable / not-elf / not-aarch64）。

### 3.2 权限边界（2026-10-19 实测）

```text
App 进程里的 Kotlin 与 JNI 同 uid，JNI 不提升权限。
/system/lib64/libc++.so   canRead=true  open=ok  size=671496  → Kotlin 直读足够
/vendor/lib64/libbinderdebug.so  canRead=false EACCES        → Kotlin/JNI 都读不到
```

因此计划只用 libc++ 作为探测目标；vendor carrier 只作为路径数据，不探测其内容。只有 crash_dump helper / 页缓存 access 才可能读 vendor，但那是攻击后的能力，不能反过来作为 plan 的输入。若将来必须探测不可读目标，才考虑 access.read，并把结果记为 unknown，首批不做。

### 3.3 ElfLayout

解析 ehdr/phdr/shdr：PT_LOAD + PF_X 的 file_offset/filesz/vaddr/memsz/align；.text/.dynsym/.dynstr/.rela.dyn/.rela.plt。每个区间先做 offset+size<=bytes.size 校验，坏表返回具名错误。

## 4. Kotlin code cave 规划

- 符号：_ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE6sentryC1ERS3_；st_value 经 exec segment 的 vaddr-file delta 转文件偏移。
- guard：bti c / paciasp / pacibsp / bti j / bti jc；site = guard ? symbol+4 : symbol；displaced 读该处 4 字节。
- 站点检查：4 字节对齐、位于可执行段、不与标准 .rela.plt 条目重叠；.rela.dyn 若是 SHT_ANDROID_RELA（紧凑编码）则跳过条目遍历。
- cave 候选：可执行段末尾页尾；段内连续 0x00/NOP 填充；段内其它页尾。
- 每个候选：16B 对齐、容量 >= 载荷 padded size、不与 relocation 重叠、B 分支在 ±128 MiB 内、落在 PF_X 段内。
- 全部失败返回 cave-missing，不退回固定 plan。

## 5. plan 与写入

Kotlin 计算 KotlinSentryPlan：siteFile/siteVaddr/landingFile/landingVaddr/payloadMax/displaced/guard/branchWord/jumpBackWord/entryOffset/displacedOffset/jumpBackOffset。

- branch：AArch64 B imm26，(landingVaddr+entryOffset)-siteVaddr，4B 对齐。
- jump-back：(siteVaddr+4)-(landingVaddr+jumpBackOffset)。
- 载荷槽：复用 Assembled43284.fillPayloadSlots 的 Kotlin 布局。
- 写入：KotlinPageCacheWriter 按 16B 切块，逐块 access.write；写后优先直接读目标文件比对，不可读时用 access.read。
- 顺序：landing 载荷 → 站点块 → carrier 模块 → trigger；恢复时先 site 后 stage。

## 6. 集成

- TypedBackendProbe 调 ElfHookPlanner.plan()，日志打印派生 site/landing/max。
- fillPayloadSlots 使用 Kotlin plan，不再依赖 Native43284.hookPlan。
- 模块写入也走 KotlinPageCacheWriter。
- native hookPlan/writeHook/writeImage 保留为未使用接口，本批不删除；生产路径不再调用。

## 7. pipe-splice 探测

Kotlin 检查 /proc/version、/proc/config.gz 与 android.system.Os 是否提供 pipe2/splice。若需要新的 vmsplice syscall，按不在 C++ 加逻辑的约束只记录结论，不实现；首批不把 pipe-splice 作为写入路径。

## 8. 批次

| 批次 | 内容 | 验收 |
|---|---|---|
| C0 | 回退未批准改动 | nativeBuild + app tests 绿；无 elf_hook/derived |
| C1 | ElfTarget + ElfLayout | 合成 ELF fixture 单测；越界拒绝 |
| C2 | 符号/guard/cave 搜索 | 模拟器 libc++ 得到 site=0x6eb54、landing 在段内且 max >= 载荷 |
| C3 | plan 计算 + branch 编码 | B 编码/跳回/±128 MiB 边界单测 |
| C4 | Kotlin writer + 直读校验 | 16B 块写/读回；不可读目标具名失败 |
| C5 | 集成 TypedBackendProbe | 模拟器 hook=1、UMH 执行、日志 finish |
| C6 | 真机门禁 | 冷机 hook=1 + 归档 |

## 9. 风险

- 模拟器与真机符号/段布局不同：plan 每次从当前文件派生。
- 紧凑 ANDROID_RELA：跳过条目遍历，不放弃标准 .rela.plt。
- cave 落在非执行页：校验 PF_X 与 vaddr/file 映射。
- B ±128 MiB 越界：候选预检。
- 直接读校验依赖目标可读：不可读时退回 access.read 并记 unknown。
- 写入顺序错会让 trigger 跑旧指令：站点块必须在 trigger 前；模块写入在站点之后、trigger 之前。

## 10. 回滚

- 任一批失败：git revert 该批。
- C0 单独一批，不与新逻辑混在一起。
- native hookPlan/writeHook 保留到 C5 验收后再决定删除。

## 11. 进度（2026-10-19）

| 批次 | 状态 | 证据 |
|---|---|---|
| C0 | 完成 | 删除 elf_hook.hpp/cpp、CMake 行；sentry_hook 恢复固定 plan；撤销 directMatches 覆盖；nativeBuildHost/Android + app tests + assembleDebug 绿 |
| C1 | 完成 | ElfTarget.kt / KotlinElfPlanner.kt；NOBITS 跳过范围校验 |
| C2 | 完成 | Kotlin 在模拟器 libc++ 上派生 site=0x6EB54、landing=0x9A160、payloadMax>=480 |
| C3 | 完成 | encodeBranch/jump-back；KotlinElfPlannerEmulatorTest |
| C4 | 完成 | KotlinPageCacheWriter + KotlinPageCacheWriterTest（逐块写/直读/access.read） |
| C5 | 部分 | Kotlin plan + writer 接入后模拟器 `hook=1`：payload 30/30 verified、site 写 1 块；模块 769 块写入、vermagic 94/158 重写成功；UMH 未触发（模拟器无 root/libc++ 触发者） |
| C5b | 完成 | 8 个 LKM 镜像全部重建为 158B vermagic 容量；`retagVermagic` 改用 `Os.uname().release`；模拟器重写 94/158B |
| C6 | 阻塞 | 当前无真机；模拟器无 crash_dump32，crash_dump64 为静态链接，app 侧触发域无 CAP_SYS_MODULE；真机门禁需硬件 |

当前只保留 Kotlin 路径的探测/规划/写入组件；C++ 仍只有原有固定 plan，无新增逻辑。
## 18. tid bypass 全系统实验（2026-10-19）

- 在 DFRoot payload 0xC8 处把 `b.ne return` 改成 NOP，让非 init 的 root 进程也进入 clone/setcon/insmod。
- 挂到系统 libc++ 后运行：系统进程崩溃/adb offline，未产生 `/dev/df` 或 UMH 日志。
- 同 payload 在 root shell 探针中（uid=0、tid bypass）完整成功：模块加载、UMH 输出 `UMH_OK`。
- 结论：payload/模块/UMH 本身成立；差异是执行域。只有 root shell/init 这类域允许 setcon/insmod；任意 root 系统进程不成立。

## 17. root 探针：DFRoot 分支完整跑通（2026-10-19）

- 编译独立 AArch64 探针 `payload_probe`，在 root shell 中 mmap 执行 DFRoot 468B payload。
- `uid=0, tid!=1`：payload return → `rc=42`，验证返回路径。
- 补丁 0xC8 的 `b.ne return`（bypass tid 检查）后：
  - `ghostlock: selinux_state set permissive`；
  - module 成功加载（`no symbol version for module_layout` 后继续）；
  - 创建 dummy run.sh 后：`ghostlock: umh exec returned 0`，脚本写出 `UMH_OK`。
- 结论：payload、clone/setcon、insmod、模块、UMH 全链路在 root shell 域成立；失败点只剩 init tid=1 触发/域。

## 16. DFRoot payload 返回路径单进程验证（2026-10-19）

- 新增 `native_payload_probe.cpp` / `NativePayloadProbe.kt`：fork 子进程，mmap 可执行页，fake site 跳 payload 0x90；payload 在 uid!=0 时 return，执行 first-inst(0x1C8) 后跳回 site+4 `exit(42)`。
- 实测：`payload probe exit=42`，证明 first-inst/jumpback/返回路径正确。
- 真实 dfroot 路径仍导致系统进程崩溃；差异只剩 init tid=1 进入 clone/setcon/insmod 的分支。
- 下一步：在受控 root/init 环境验证 clone/setcon，或确认模拟器 init 与 DFRoot 真机 init 的差异。

## 15. DFRoot 468B payload 接入（2026-10-19）

- 从 DFRoot 源码编译 `libcxx.S`：468B，entry=0x90、first_inst_copy=0x1C8、jumpback=0x1CC、len=0x1D0；与旧 472B blob 布局不同。
- 新增 `DfrootLibcxx.kt` 嵌入该 468B；Kotlin 在 sentry site/landing 上直写：
  - 0x1C8 ← 原入口指令；
  - 0x1CC ← 回跳字；
  - 站点 ← `b landing+0x90`。
- 首次运行导致模拟器测试进程 segfault，系统进程不稳定；默认目标已回退到旧 libc++ 路径，`hook=1` 稳定。
- 需要下一步定位：DFRoot payload 的 first-inst/jumpback 偏移或触发域，避免系统进程执行时崩溃。

## 14. 静态入口 shellcode（2026-10-19）

- `EntryPayload.kt`：写 `/proc/self/attr/exec = u:r:vendor_modprobe:s0`，再 execve `/vendor/bin/insmod /vendor/lib64/libbinderdebug.so`。
- 直写把 13 块 payload 写到 crash_dump64 0x430，站点块 0x420 写 `b 0x430`；读回 `first=ff 83 00 d1 ...` 正确。
- 诊断 `exitOnly()`：`static detonate exit=42`，证明入口分支与 shellcode 执行成立。
- 真实 setcon+insmod：`static detonate exit=13`（EACCES）；换成 `/system/bin/insmod` 同样 13。crash_dump 域不允许 setcon vendor_modprobe，也不允许执行 insmod；DFRoot 的同类 setcon/insmod 发生在 init tid=1 上下文。
- 直接 `init_module` 版本：`static detonate exit=159`（128+31，SIGSYS）—— crash_dump 的 seccomp 过滤拦截了 init_module。setcon/exec insmod 为 EACCES，直接 init_module 为 SIGSYS。
- 结论：直写与入口 payload 都已成立；剩余是 crash_dump 域的 SELinux/seccomp 限制，DFRoot 的 insmod 发生在 init tid=1 上下文。

## 13. Kotlin 直写（DFRoot 同款，2026-10-19）

- 新增薄 JNI `core/src/boundary/jni/native_raw_esp.cpp`：`open(socket)`、`writeBlock(vmsplice header + splice file page + splice pipe->UDP)`、`close`；无 plan/IV/校验逻辑。
- 新增 `NativeRawEsp.kt`、`RawEspWriter.kt`：Kotlin 读旧内容、AES-ECB-DEC 算 IV、分块、序号、校验。
- 新增直写专用 SA：AES-CBC、**无认证**（DFRoot `ExplorerRunner` 同款），与现有 landing SA 分开；Kotlin 持有 SPI/端口/密钥。
- 探针（app cache 文件）成功：
  ```text
  raw esp probe ok=true back=52 41 57 45 53 50 2d 50 52 4f 42 45 2d 30 30 31
  ```
- 目标差异已定位：crash_dump64 **offset 0 与 0x400（页对齐）直写成功**，`0x430`（页内偏移 0x30）失败；libc++ `0x9A160`（非页对齐）成功。结论：模拟器 APEX 上 `splice` 需要页对齐偏移；DFRoot 的 helper 也是写 offset 0。静态入口 0x428/0x430 不能用当前 raw writer 直接写，需要页对齐的 helper/整页方案。
- 静态入口路径已用 `/data/local/tmp/.gl_static_hook` marker 显式开启；默认仍走 libc++ landing，`hook=1` 保持。

## 12. 静态 crash_dump64 入口实验（2026-10-19，历史记录）

> 本节为实验过程；当前权威结论见 `docs/analysis/dfroot-parity-review.md` §8。

- 新增 `KotlinElfPlanner.planEntry()`：site=e_entry、landing=可执行段末尾页尾；`/tmp/crash_dump64` 测试得到 site=0x428、landing=0x430、max=3024。
- `PlanClosure` 支持 `hookTargetSize`：镜像在 carrier、载荷/站点在 hook target 时分开校验重叠。
- 接入 crash_dump64 后，`hook ids target=21474836481 surface=3 landing=3 access=2 door=21474836481`；native 读回 0x430 的 16 字节为 `2f 64 65 76 2f 64 75 00 ...`，正是原文件的 `/dev/du` 字符串，不是 payload。改用 native `writeImage` 也返回 `wrote=true code=0`，但 crash_dump64 的 0x430 仍是原字节；随后 `hook_restored=1`、模块写入报错。结论：写入目标绑定在 native landing，Kotlin-only 层改不了；已回退生产 target 到 libc++。
- 结论：静态入口 plan 本身可用，但 native 多目标块写不支持；已回退生产 target 选择到 libc++，保留 `planEntry` 与 separate-target 闭包作为后续基础。

