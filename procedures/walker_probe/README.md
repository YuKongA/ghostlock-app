# walker_probe — 系统 libc++「踩踏」最小探针（真机 QV770MFGJ1）

## 1. 目的

在真机上验证一个**最小 C++ 程序**是否让进程执行到 `/system/lib64/libc++.so` 页缓存里的
walker 钩子（「踩」），并读取三标记 `/dev/df`、`/dev/dfm0`、`/dev/dfm1`。

- 钩子站点：file `0xB30AC` = `0x14012D83`（`B +0x4B60C` ⇒ 载荷入口 file `0xFE6B8`）；
  站点所属符号 `_ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE6sentryC1ERS3_`
  （`std::__1::basic_ostream<char>::sentry::sentry`；vaddr `0xB70A8`，`bti c` 之后 +4 被替换）。
- `std::cout` 的每个输出操作都会构造哨兵 ⇒ 一次 `std::cout` 即可「踩」。
- 本探针 = 最小载体：`std::cout.put` 打一行；stderr 上的两条括注走 bionic libc，不经过 libc++，不会踩。

## 2. 编译链路的关键事实（本会话实测）

**纯 NDK 产物绑定不到系统 libc++**（两边符号命名空间不同），这是本探针必须按平台 ABI 手写声明的原因：

- NDK 30.0.16248370 头文件侧：`.../include/c++/v1/__config_site:12-13` 硬定义
  `_LIBCPP_ABI_VERSION 1` 与 `_LIBCPP_ABI_NAMESPACE __ndk1`；命令行
  `-D_LIBCPP_ABI_NAMESPACE=__1` 会被其覆盖（实测编译产物仍为 `St6__ndk1`）。
- NDK 的 `libc++_shared.so` 导出 1909 个 `St6__ndk1` 名字、**0 个** `St3__1`。
- 设备平台 libc++ 导出 `St3__1` 名字（真机文档 `docs/analysis/device-gates/B5-9h-20261004-device-assets-recon.md:8`）。
- 设备 `/system/lib64` 只有 `libc++.so`（1083168 B，inode 2421），**没有** `libc++_shared.so`；
  且静态 libc++ 不会踩（任务前提本身即如此警告）。

⇒ 常规 NDK 编译（静态或动态）都到不了钩子。本探针的做法：`main.cpp` 不包含任何 libc++ 头文件，
**手写最小平台 ABI 声明**（`std::__1` 的 `cout` / `put` / `flush` / `ios_base::Init`），
`-nostdlib++` 抑制 NDK libc++，链接输入用**设备 `libc++.so` 的链接期副本**，
使 `DT_NEEDED = libc++.so`（该文件 SONAME），运行时由设备解析到真正的（已被改动的）文件。

## 3. 编译（macOS + 本仓库 NDK）

```bash
# 链接期输入（一次性、只读）：拉取同一镜像的 libc++.so
adb -s QV770MFGJ1 pull /system/lib64/libc++.so /tmp/glk_walker_build/libc++.so

# 编译 + 形状自检（不合格即具名报错退出；产物落在 OUT_DIR，不进仓库）
./build.sh /tmp/glk_walker_build
```

等价的编译行（`build.sh` 内部；NDK 解析顺序 `local.properties ndk.dir` → `$ANDROID_NDK_HOME`
→ `$ANDROID_NDK_ROOT` → `~/Library/Android/sdk/ndk/*` 最新）：

```text
$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android30-clang++ -O1 -fPIE -pie -nostdlib++ main.cpp <OUT_DIR>/libc++.so -o <OUT_DIR>/glk_walker
```

与草稿命令的两点差异：① 增加 `-nostdlib++`（防 NDK libc++ 符号混入）；② 追加设备
`libc++.so` 作链接输入（否则没有符号提供者）。**未**使用 `-static-libstdc++` 或任何静态 libc++。
（本探针为设备侧/运维性质，按用户指示以 shell 落在此目录，不进 Gradle 构建图。）

## 4. 运行（设备，原文）

```bash
adb -s QV770MFGJ1 push /tmp/glk_walker_build/glk_walker /data/local/tmp/glk_walker
adb -s QV770MFGJ1 shell 'chmod 755 /data/local/tmp/glk_walker && /data/local/tmp/glk_walker; echo exit=$?; ls -l /dev/df /dev/dfm0 /dev/dfm1 2>&1 | head -3'
```

## 5. 本会话实测（2026-10-16）

### 5.1 基线复核（只读；早于任何探针运行）

命令（只读）：`dd if=/system/lib64/libc++.so bs=1 skip=733356 count=4 | od -An -tx1`、
`ls -l /system/lib64/libc++*`、`stat`（inode）、`ls -l /dev/df /dev/dfm0 /dev/dfm1`、
`grep -i kernelsu /proc/modules`。原始输出：

```text
DEV: A301SO rel=15 sdk=35
PATCH_BYTES:
 83 2d 01 14
SYS64_LIBS:
ls: /system/lib64/libc++_shared.so: No such file or directory
ls: /system/lib64/libc++abi.so: No such file or directory
ls: /system/lib64/libunwind.so: No such file or directory
-rw-r--r-- 1 root root 1083168 2008-12-31 19:00 /system/lib64/libc++.so
INODES:
2421 1083168 /system/lib64/libc++.so
MARKERS:
ls: /dev/df: Permission denied
ls: /dev/dfm0: Permission denied
ls: /dev/dfm1: No such file or directory
MODULES:
kernelsu 217088 1 - Live 0xffffffd925b2b000 (OE)
```

### 5.2 探针运行 —— **未执行**（停手条件在基线即命中）

任务给定的停手规则：「若你看到 `kernelsu` 出现在 `/proc/modules` 或三标记出现，**立刻停手**」。
基线即命中：`kernelsu` 已在 `/proc/modules`；`/dev/df`、`/dev/dfm0` 已存在。因此：

- **探针未运行（0 次触发）、未 push、未做任何设备写入**；设备侧命令全部只读
  （`getprop` / `dd` 读 / `ls` / `stat` / `grep`）。
- 设备现处「已加载」态，与 `docs/analysis/device-gates/43284-full-chain-lkm-pass-20261016.md`
  （2026-10-16 20:46-20:48 的全链 PASS：`kernelsu 217088 1 - Live 0xffffffd925b2b000 (OE)`、
  `/dev/dfm0` 在、`/dev/df` 亦在、`/dev/dfm1` 不在）逐字一致 ⇒ 应是该 PASS 留下的内存态（未冷启动）。

### 5.3 三标记结果（基线值）

- `/dev/df`：**存在**（shell 的 `ls -l` 被拒 ⇒ `Permission denied`；`ENOENT` 与 `EACCES`
  的差异即存在性判据）— 语义：载荷互斥/in-flight 标记（`core/src/tools/pagecache_load_lkm.cpp:143`）。
- `/dev/dfm0`：**存在**（同上）— 语义：模块加载**成功**标记（`core/src/tools/pagecache_load_lkm.cpp:142`）。
- `/dev/dfm1`：**不存在**（No such file or directory）— 语义：加载**失败**标记（`core/src/tools/pagecache_load_lkm.cpp:141`）。

## 6. 本地编译验证（同一会话；未上设备）

本机无法执行 aarch64 产物，验证到「链接形状」。为替换真实的设备 `libc++.so`，用 NDK
`libc++_shared.so` 做**替身**（`.dynstr` 中 `St6__ndk1`→`St3__1` 原位改写、
SONAME 改为 `libc++.so`）：

```text
dynsym defs with St3__1:    1909
dynsym defs with St6__ndk1: 0
SONAME: Library soname: [libc++.so]
（探针需要的五个符号在替身中按平台名定义：
 _ZNSt3__14coutE / _ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE3putEc /
 _ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE5flushEv /
 _ZNSt3__18ios_base4InitC1Ev / _ZNSt3__18ios_base4InitD1Ev）
```

`./build.sh /tmp/glk_walker_build` 输出：

```text
== DT_NEEDED ==
  0x0000000000000001 (NEEDED)          Shared library: [libc++.so]
  0x0000000000000001 (NEEDED)          Shared library: [libm.so]
  0x0000000000000001 (NEEDED)          Shared library: [libdl.so]
  0x0000000000000001 (NEEDED)          Shared library: [libc.so]
== ok: /tmp/glk_walker_build/glk_walker ==
```

探针动态未定义符号（`llvm-nm -D -u`）：

```text
                 U _ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE3putEc
                 U _ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE5flushEv
                 U _ZNSt3__14coutE
                 U _ZNSt3__18ios_base4InitC1Ev
                 U _ZNSt3__18ios_base4InitD1Ev
                 U __cxa_atexit@LIBC
                 U __libc_init@LIBC
                 U __register_atfork@LIBC
                 U fprintf@LIBC
                 U fwrite@LIBC
                 U getpid@LIBC
                 U stderr@LIBC
```

产物：8312 B、`DYN (Shared object file)`、`AArch64`、PIE；`-Wall -Wextra` 零告警。
**限制**：以上用替身完成；「设备平台是否导出这五个名字」的最终校验，需对着**真实拉取**的
`libc++.so` 再跑一次 `./build.sh`（链接期会具名列出缺失符号）。本次未拉取（停手）。

## 7. 判读

1. 前提成立且与仓库记录一致：`0xB30AC` 页缓存 = `0x14012D83`（对 imm26 的独立解码得目标
   `0xFE6B8`，与载荷入口吻合）；设备处于 2026-10-16 20:46 全链 PASS 留下的已加载态。
2. **原任务设想的链路不成立**：NDK 头文件产 `__ndk1`、平台导 `__1`，且设备无
   `libc++_shared.so` ⇒ 常规 NDK 编译（无论静态/动态）在设备上要么符号解析失败、要么不踩。
   本目录的 `main.cpp` / `build.sh` 是修正后的做法，已在本地把链接形状验证到绿。
3. 三标记当前值（df/dfm0 在、dfm1 不在）与 PASS 记录逐字一致 ⇒ 不是新事件，不能用它判断
   「探针是否踩了」。探针自身的「踩」证据要看：干净态复跑（§8），或 `exit=139` + stderr 只到
   `begin`（进程内证据）。
4. 未执行项（因停手）：① 真机运行 1 次；② 对真实 `libc++.so` 的链接期全量符号校验。

## 8. 若要继续（干净态复跑）

1. 只读拉取链接输入：`adb -s QV770MFGJ1 pull /system/lib64/libc++.so /tmp/glk_walker_build/libc++.so`
2. `./build.sh /tmp/glk_walker_build`（链接期完成全量符号校验；缺符号会具名报错）。
3. 设备复跑（**≤3 次**；出现 `/dev/dfm1` 或 `/proc/modules` 中出现 `kernelsu` 即**立即停手**）：见 §4 命令原文。
4. 读法：`exit=139` 且 stderr 只有 `[walker] begin ...` ⇒ 已踩、载荷在进程内跑过（与
   `crash_dump64` 行为一致）；`exit=0` 且三行齐全 ⇒ 未走到钩子（查 §6 链接形状）或走到但载荷放行；
   出现 `/dev/dfm1` ⇒ 载荷走了失败分支；出现 `/dev/dfm0` 新增 ⇒ 模块加载成功（立即停手）。
5. 干净态前提：需冷启动（重启会同时清掉页缓存补丁与已载模块，须重新走链）后再跑。
