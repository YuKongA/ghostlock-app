# 43284：成功历史 vs 当前实现 —— 逐条差异表

> 基准（成功历史）：git 修订 `ref` = **6f7ded87**（2026-10-04「43284 full chain PASS with self-built LKM」），源码根 `src/core/…`。
> 现状：工作树 `dev-nocpp`（`core/src/…` 新布局）+ 临时探针。清点见 `retired-chain-inventory.md`、`current-chain-inventory.md`。

## 一、成功那次的判据原文（对照锚点）

`docs/analysis/device-gates/B5-9h-20261004-full-chain-lkm-pass.md:15-18`：

```
write: written=769 verified=769 crash_dump=1 hook_planned=1 hook_armed=1 hook=0
trigger: fired=1 / run.wait outcome=LkmLoaded terminus=1
uid=0 / Enforcing / kernelsu=1 + ksu.log: [*] late-load kmi=android13-5.15 · exit=0 → KernelSU module loaded
```

**要点**：`hook=0` —— **libc++ 钩子没有生效**，模块仍然加载。⇒ 加载的必要条件不是钩子，而是：
**patch#1（90 块）+ carrier 写 769 块逐块回读全过 + trigger + wait 判定 LkmLoaded**。

## 二、逐条差异

| # | 项 | 成功历史 | 当前实现 | 判定 |
|---|---|---|---|---|
| 1 | 链序 | patch#1 → **carrier** → hook → trigger → wait（chain.cpp:400-403/452-470） | 探针 hook(:803) **早于** carrier(:969) | ✗ 不同（工具链 `pagecache_load_lkm.cpp:527/589/633` 才是对的） |
| 2 | 单位/对齐 | 16 B；carrier 逐块；hook 对齐块（chain.hpp:51） | 16 B ✓；仅 `write_hook` 做对齐 | ✓ 基本同（`write_image_verified` 无对齐） |
| 3 | ESP 报文 | SPI4+Seq4+IV16+cipher+ICV；IV=ECB_DEC(K,old)^desired（ipsec.hpp:47-58） | `SPI(4)+Seq(4)+IV(16)+cipher+ICV(16)=56 B`；ICV=HMAC-SHA256 截断（pagecache_write.hpp:145-171、esp_crypto.hpp:59-74） | ✓ 同构 |
| 4 | 序号 | 成功后 `next_seq+1`；patch#1/hook **共享 SA/socket**，apply 前拷入、后拷回（real_ops.cpp:239-257/303-307/321-325/336-342） | 窗口 open 一律回 1 ✗；跨窗口靠显式 `nextSequence/adoptSequence` ✓（探针已 adopt 到 91 ✓） | 有回归 |
| 5 | **序号参数通路** | `sequence_set` 随初值一起进 access | `device_access_initialize` 只拷 `params.sequence`，**没拷 `sequence_set`**（api_43284_device.cpp:895）⇒ `has_sequence()` 假 ⇒ access 不 adopt（kernel_access.hpp:76-82） | ✗✗ **回归**（被我 893fce97 的重排吃掉） |
| 6 | 触发 | double-fork 孤儿；默认 1000ms（real_ops.hpp:69），分阶段 500ms（stage_runner.cpp:999）；exec_path 默认 nullptr | double-fork 孤儿 ✓；用 500ms ✓；exec 未用 ✓ | ✓ 同 |
| 7 | 等待/终态 | `/dev/dfm1`⇒Failed · `/dev/dfm0`⇒**LkmLoaded** · `/dev/df` 非终态继续轮询 · `/sys/module/dirtyfrag` present→gone⇒Failed；**10ms 轮询 / 5000ms 预算**（real_ops.cpp:391-432、chain.hpp:196） | 探针用 15000/100 ✗；`moduleMarker` 未接进 `WaitRequest.module_dir`（api_43284_device.cpp:187-195） | ✗✗ 不同 |
| 8 | 载体规则/回退表 | 绝对+<64B+无空白+`/vendor 或 /system/vendor`（chain.cpp:19-42）；默认 **4 个 vendor 载体**（chain.hpp:70-75） | 只用一个载体；无回退表 | ✗✗ 缺失（M4） |
| 9 | 写前闭合 | fstat/plan 闭合/PreImageMismatch（chain.cpp） | 无 | ✗✗ 缺失 |
| 10 | 回滚 | `restore_hook_patch` 在链内 | **链内无 restore**；只有探针 marker 门控的手动恢复（TypedBackendProbe.kt:938-950） | ✗✗ 缺失（M1） |
| 11 | hook 计划 | ELF 运行期推导（elf_hook.cpp） | 声明值常量（sentry_hook.cpp:47-78） | ✗ 不同（可接受，但换机即失效） |
| 12 | hook 写内序 | **先 shellcode 块、后 trampoline 块**（hook_patch.cpp:176-190） | native `write_hook` **先站点后载荷**（pagecache_steps.cpp:121-150） | ✗ 相反 |
| 13 | 槽内容 | mutex `/dev/df`；selinux 长度立即数（hook_patch.cpp:302-311） | mutex `/dev/dx` ✗；**长度立即数少 1**（Assembled43284.kt:218） | ✗ 两处不一致 |
| 14 | 生产终局 | 设备事实/KMI/UMH/secrets/root 判据（backend_terminal.cpp:129-156、lkm/*） | 未移植 | ✗✗ 缺失（M6） |
| 15 | 模块预检 | vermagic 预检进链 | 探针喂假镜像（TypedBackendProbe.kt:318-323） | ✗ 缺失（M5） |
| 16 | profile 读取 | 执行路径读 profile 键 | 未读（数值写死在 Kotlin） | ✗ 待办（用户已裁定：先写死） |

## 三、最短收敛路径（按成功判据排序）

1. **carrier 769 块写+逐块回读全过**（对应 `written=769 verified=769`）。当前卡在 `image-write:step-verify-failed`：写报成功而该偏移内容不变。
   - 依赖：#5（序号参数通路断）先修；#8（载体回退表）确认目标载体与 SA 匹配。
2. **trigger**（已同 ✓：double-fork + 500ms）。
3. **wait 判 `/dev/dfm0` ⇒ LkmLoaded**（#7：把 15000/100 改成 5000/10，并把 module marker 接进 `WaitRequest`）。
4. **UMH late-load → `kernelsu`**（#14 的契约面：仅干净终态才填 UmhForwardInput）。
5. 之后才轮到 libc++ 钩子（成功那次 `hook=0` ⇒ 非必要；#12/#13 留作后续）。

## 四、当前现场读数（供收敛时比对；仅现场可得）

- 站点块写后复读 `verified=true`（0xB30AC 已是 0x14012D83 ⇒ 指向载荷入口 0xFE6B8）。
- 序号两条链已连续：`nextSequence before hook=91` → `adopt seq=91` → 载体/载荷/站点同一链。
- carrier 自检：`block[16] verify failed: want 0100b700 got 0300b700`；helper `spliced=16` 且 `head=0300b700`（与 got 一致 ⇒ 读侧自洽，写未改变该偏移）。
- 无 `/dev/dfm0` `/dev/dfm1`；`kernelsu`=0；设备稳定（多次运行未崩未卡，除早期两次因错误站点写入引发的 panic/卡死，已修）。

## 五、2026-10-16 会话新增硬事实（真机实证，均已复核）

| 事实 | 证据 | 含义 |
|---|---|---|
| 钩子与载荷**此刻仍在全局共享页里** | `adb shell dd if=/system/lib64/libc++.so bs=1 skip=733356 count=4` ⇒ `83 2d 01 14`；`skip=1042104 count=4` ⇒ `e0 07 bf a9` | 写阶段彻底成立：跨运行、跨分钟仍在；任何走该 sentry 的进程都会起火 |
| helper（桥）通道**完全正常** | `adb shell crash_dump64 16 /vendor/lib64/libbinderdebug.so` ⇒ 输出 `01 00 b7 00 …` | 与自检 `want 0100b700` 同源 ⇒ 自检口径正确；helper argv/stdout 管道均按实测约定工作 |
| `crash_dump32` **不打印** | 无参 / `1 1 11 1` / `<offset> <path>` 三种调用全部 `exit=0` 且零输出 | 排除它作为点火者的可能 |
| 三个 shell 域 C++ 进程常驻且映射 libc++ | `shizuku_server`(12 段) · `brevent_daemon`(4) · `brevent_server`(16) | 它们**当前**不走 `ostream` ⇒ 不是自动点火者 |
| 载体写入 **769/769 全过** | 应用耐久日志 `typed: module wrote=true code=0 refusal=` | 等同成功历史的 `written=769 verified=769` |
| 序号链路连续 | `carrier_next_after=91` ⇒ 站点/载荷/载体同一条 SA 序号 | 对齐 real_ops.cpp:306-307 的纪律 |
| 槽值全部对齐参考 | SELinux 长度含尾 NUL（+1）· mutex `/dev/df` | 依据 hook_patch.cpp:303 与 hook_patch.hpp:146 逐字 |
| 载荷在 **app 域**起火但无权限 | 应用域执行 `crash_dump64` ⇒ `detonate exit=139`（SIGSEGV）；三标记全无 | “踩”已被证明可行 ⇒ **剩唯一变量＝执行域** |
| 设备资产完好 | `/data/local/tmp/.ghostlock_lkm_cmd.sh`(755) · `.ghostlock_root.sh` | 一旦加载成功，UMH 日志会刷新（现停在 10-10 ⇒ 从未成功） |

### 当前唯一未解变量：**在哪个域里被“踩”**

- 设计本意：载荷把 `/proc/self/attr/exec` 设成 `u:r:vendor_modprobe:s0` 并调 `/vendor/bin/insmod`，因此它**必须**在允许该操作的特权域里运行。
- 已排除：app 域（被拒 ⇒ 139）· `crash_dump32`（不打印 ⇒ 不踩）。
- 进行中（三路并行）：A 逐行 verbose 落码；B 退役链复现评估与对比字段清单；C 自建“动态链接系统 libc++ 且会打印”的 shell 域程序。

### 判据（不变）

`/dev/df`（载荷开始运行）⇒ `/dev/dfm0`（成功）/`/dev/dfm1`（失败）⇒ `/proc/modules` 含 `kernelsu` ⇒ UMH 日志刷新。
