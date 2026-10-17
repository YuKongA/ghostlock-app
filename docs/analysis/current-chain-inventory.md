# 当前链清点（dev-nocpp 现状）：CVE-2026-43284 工作树链实现

> 清点对象 = **当前工作树** HEAD `08b08cd3`（分支 `dev-nocpp`，2026-10-16 20:00），布局 `core/src/…` + `app/src/main/kotlin/…`。
> 清单与 `docs/analysis/retired-chain-inventory.md` 同构（对面按退役链 6f7ded87 逐项清点的同一张清单），便于直接对拍。
> 只读清点：除本文件外未改任何文件、未跑 gradle/构建；git 只用只读形式。源码证据一律 `file:line`（相对仓库根；`device-gates` 里 2026-10-15 的三份记录是单行文件，引用一律给 `:1`）。
> 退役侧证据用 `6f7ded87:<path>:line` 或对面已入库的 `docs/analysis/retired-chain-inventory.md:NN`。

## 0) 基准与范围

| 项 | 值 | 证据 |
|---|---|---|
| 修订 / 分支 | `08b08cd3` · dev-nocpp（工作树有用户既有改动 + 未跟踪的 procedures/、profile/） | `git log -1`；`git status --short` |
| 链的四个入口 | ① JNI 薄原语（35 导出）② Kotlin 装配线 `Assembled43284` ③ 封装步骤 `steps::run_pagecache_step`（5 个名字）④ 工作示例工具 + Lua | `core/src/jni/native_43284.cpp` 导出名清单；`app/src/main/kotlin/com/ghostlock/app/jni/cve43284/Assembled43284.kt:30-33`；`core/src/backend/cve_2026_43284/steps/pagecache_steps.hpp:26-39`；`core/src/tools/pagecache_load_lkm.cpp:15-30`；`procedures/pagecache_load_lkm.lua:6-7` |
| 真机链路当前由谁驱动 | **探针** `TypedBackendProbe.runOnce → probe43284 → probeAssembled43284`（屏幕上/设备门禁按钮），不是生产入口 | `app/src/main/kotlin/com/ghostlock/app/tempui/TempUiActivity.kt`（接线）→ `TypedBackendProbe.kt:94-231`、`:582-1042` |
| 生产入口状态 | profile 键已存在（`execution.write.block_bytes / trigger.delay_ms / wait.budget_ms / wait.poll_ms`），但**执行路径不读它们**：探针用 Kotlin 常量 | `profile/profile-schema.json:127-146`；`TypedBackendProbe.kt:985-999` |
| 工作示例工具的阶段 | `credentials → module → patch1 → hook → write → trigger → wait`（7 phase，内部顺序 = patch1 → write → hook → trigger → wait） | `core/src/tools/pagecache_load_lkm.cpp:15-30`、`:465/:474/:527/:589/:633` |

## 1) 阶段与顺序（**现探针实际顺序**）

探针一次运行 = 建 SA/落帧 → 43499 探针 → 43284 原语探针 → 装配线链（stepwise/patcher/hook/module）→ trigger → wait → 收尾：

| # | 动作（谁写的） | 写什么/目标 | 证据 |
|---|---|---|---|
| 0 | `deployUmhPayload` | 把 `assets/payload/root_cmd.sh` 拷到 `/data/local/tmp/.ghostlock_lkm_cmd.sh` 并 chmod +x（模块自带命令的落点） | `TypedBackendProbe.kt:104`、`:556-569` |
| 1 | Kotlin 直连 IpSecManager | ① 申请 SPI 并读回；② 开 4500 封装套接字；③ 自造 SA 帧写 `/data/local/tmp/ghostlock-sa.frame`；④ 正式建 SA（开 encap → 取真实 sender 端口 → 申请 SPI → `CRYPT_AES_CBC`+`AUTH_HMAC_SHA256(128)` → `setIpv4Encapsulation` → `buildTransportModeTransform(loopback)`）→ 再落一次帧 | `:113-126`、`:133-145`、`:147-174`、`:176-226`（`:138` 4500；`:187-190` sender 端口；`:209` ICV 128 bit） |
| 2 | `probe43499` | 43499 库最小序列（与 43284 链无关） | `:228`、`:233-276` |
| 3 | 43284 原语探针 | create → initialize → snapshot → embeddedImage(0) → hookPlan("splice") → moduleCheck(假镜像) → waitTerminal(40 ms) → resolveTarget(开发载体) → readBlock → **patch1-via-kotlin**（helper 1440 B/90 块经 writeBlock 逐块写 + 回读）→ writeBlock(0x33@0) → resolveTarget(坏路径) → **trigger(150 ms)** → shutdown/destroy | `:285-457`（`:380-422` patch1-via-kotlin；`:451` trigger 150） |
| 4 | `probeAssembled43284` A：session | 开门（`doorPath`）→ 读一块（只读自证） | `:592-602` |
| 5 | 装配线 A：`stepwise`（doorPath） | `openTargetStepwise → openEspSocket → buildSurface → buildLanding → writeHelperThroughLanding（经 landing 直写 helper，**无校验**）→ readLandingFirstBlock → buildAccess → initializeAccessAndRemember(载体参数) → writeImage(helper,0,16,**budget 200 ms**) → destroyAccessNow`；其间 `bringUp=skipped-path-b`（Access::initialize 会 self bring-up） | `:604-680`（`:611-618` 花样读写 0x1000；`:626` skip；`:628` helper-through-landing；`:678` writeImage） |
| 6 | 装配线 B：`patcher`（**patch #1 = crash_dump64**） | `openTargetStepwise(/apex/com.android.runtime/bin/crash_dump64) → reuseSocketFrom(stepwise) → buildSurface → buildLanding → buildAccess → initializeAccessAndRemember → writeImage(helper,0,16)`；随后 **Kotlin 直读 crash_dump64 前 48 B 与经链读回对比**、打印 nextSequence/xfrm_stat/encap 状态/直发 UDP 探针 | `:689-801`（`:732-745` old-page 对照） |
| 7 | 装配线 C：**hook（libc++）** | `openTargetStepwise(/system/lib64/libc++.so) → reuseSocketFrom → adoptSequence(stepwise.nextSequence())` → surface/landing/access → `initializeAccessAndRemember(saInit(patcher.nextSequence()))` → 只读取样（0xFE600 及邻域，**花样写已拆除**，`wrote=-99`）→ hookPlan → 站点改写（`rewriteSiteBranch(0,…)` 自算路**已停用**，`-1`）→ "站点 image 提前写"（`imWrote=-77` 已拆除）→ `adoptSequence` 第二次 → `writeHook(16)` → 载荷填槽 + `writeImage(payload, plan[3]=0xFE628, 16)` → **站点块**（`siteOff=plan[2]`，整块读改写，词取 `plan[0]`）→ 回读断言 `verified`（`/data/local/tmp/.gl_restore` 存在时改走原厂 16 B 恢复，目标 733344=0xB30A0） | `:803-1030`（`:813`/`:897` adopt；`:825`/`:885` 停用哨兵；`:909` 填槽；`:911` 载荷；`:915-963` 站点块 + restore + verify） |
| 8 | 装配线 D：**module → vendor 载体** | 读 `/data/local/tmp/ghostlock.ko` → 门 `/vendor/lib64/libbinderdebug.so`（打不开 ⇒ 桥）→ 重复 A 的 socket/surface/landing/access → `initializeAccessAndRemember(saInit(patcher.nextSequence()))` → `writeImage(module,0,16)` | `:968-987` |
| 9 | **trigger** | `delay = accessTriggerDelayMs(500) > 0 ? 500 : 2000`；`hook.trigger(delay)` → native `steps::trigger_orphan`（double-fork） | `:988-991`；`core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:152-175` |
| 10 | **wait** | `budget 15000 / poll 100`；success `/dev/dfm0`、failure `/dev/df1`、inflight `/dev/df`、`moduleMarker=/sys/module/dirtyfrag`、`moduleName=kernelsu` | `:992-1001` |
| 11 | 收尾 | ksud 状态只读（sysfs 模块 / umh 脚本 / lkm ok+fail / ksud 三路径 / 四条 marker / `ghostlock-ksu.log` 尾 3 行）→ patch1/stepwise 各自 `destroyAccessNow/tearDown` → destroy 句柄 → session.close | `:1002-1041` |

**顺序事实（与退役链对照）**：探针把 **hook 放在 carrier（module）之前**（`:803` ①hook 段、`:969` ②module）；退役链与当前 **step/工具**侧都是 `patch1 → carrier 写+校验 → hook → trigger → wait`（`6f7ded87:src/core/backend/cve_2026_43284/steps/chain.hpp:107-121`；`core/src/tools/pagecache_load_lkm.cpp:527/589/633`；`docs/analysis/retired-chain-inventory.md:33`）。
**native `steps::write_hook` 的内部顺序也与参考相反**：它先写站点块、后写载荷（`pagecache_steps.cpp:121-150`），参考是**先 shellcode/载荷块、后 trampoline/站点块**（`6f7ded87:src/core/backend/cve_2026_43284/steps/hook_patch.cpp:176-190`）；探针走的是"载荷→站点"（`:911` 先于 `:915`），与参考一致，但**绕过了 `write_hook`**。

## 2) 全部常量

### 2.1 Kotlin 里写死的数值（探针 / 包装）

| 常量 | 值 | 证据 |
|---|---|---|
| 块宽 `BLOCK` | 16 B | `Assembled43284.kt:593` |
| 目标角色 | `PATCH1=1 / CARRIER=2 / HOOK=3 / REMOTE_BRIDGE=4` | `Assembled43284.kt:596-599`；native 同序 `core/src/jni/api_43284.hpp:37-43` |
| 自带镜像角色 | `0=splice helper / 1=landing payload` | `Assembled43284.kt:602-603`；`api_43284.hpp:97-98` |
| 固定目标 | patch1 `/apex/com.android.runtime/bin/crash_dump64`；hook `/system/lib64/libc++.so` | `Assembled43284.kt:556`、`:559` |
| 会话材料（探针哨兵） | `spi=0x1234 / encap=4500 / sender=0 / icv=16 / aes=0xA5×32 / hmac=0x5A×32 / seq=1(present)` | `TypedBackendProbe.kt:502-512` |
| 会话材料（写路径） | 真 SPI/encap/sender；`icvLength=16`；密钥副本 `aes=0xA0+i`、`hmac=0xB0+i`；`initialSequence = adopt>0 ? adopt : 1`、`sequencePresent = adopt>0` | `:152-153`、`:187-196`、`:526-546` |
| 写校验耐心 | `WriteImage43284.imageBudgetMs = 200`（Kotlin 给） | `Assembled43284.kt:485-487` |
| Access 参数常量 | `carrier_base=0`、`offset=0`、`offsetSet=true`、`hookPlanSet=false`、`preempt/modversions/forceUnload=false`、`waitMs=0` | `Assembled43284.kt:575-586` |
| **触发延时** | `accessTriggerDelayMs = 500`（参考 execution_binding.cpp:170 默认值） | `Assembled43284.kt:584-585` |
| 探针触发延时 | `trigger(150)`；hook 段 `delay = 500 或回退 2000` | `TypedBackendProbe.kt:451`、`:988-991` |
| **mutex 槽内容** | `/dev/dx`（注释：不用 `/dev/df`，那是 10-05 遗留的陈旧 in-flight 标记） | `Assembled43284.kt:183`、`TypedBackendProbe.kt:909` |
| **ko_target 槽内容** | 载体路径 `/vendor/lib64/libbinderdebug.so`（参考 backend_terminal.cpp:321：载体与 ko_target 同名） | `TypedBackendProbe.kt:909`、`:973` |
| 其余槽内容 | selinux `u:r:vendor_modprobe:s0`；insmod `/vendor/bin/insmod`；attr `/proc/self/attr/exec`；displaced=`plan[1]` | `Assembled43284.kt:181-186`、`:234-238` |
| 槽表（偏移/容量） | mutex 0x00/8 · selinux 0x08/23 · 长度指令 0x16C/4 · attr 0x1F/21 · insmod 0x34/19 · ko 0x47/64 · displaced 0x1D0/4 · 回跳 0x1D4/4 | `Assembled43284.kt:167-169`、`:203-238` |
| 长度立即数 | `0xD2800000 \| (len<<5) \| 2`，**取 `selinuxContext.length`（不含 NUL）** | `Assembled43284.kt:218`（← 与参考差 1，见 M10） |
| **文件→vaddr 换算 0x4000** | `jumpSelf = payloadFile + 0x4000 + 0x1D4`；`jumpTo = hookFile + 0x4000 + 4`；B imm26=`(to-from)>>2` | `Assembled43284.kt:224-233` |
| 分支词 | `0x14000000 \| imm26`（站点用 plan[0]；自算路已停用） | `TypedBackendProbe.kt:838-857`、`Assembled43284.kt:141-163` |
| 恢复（手动） | marker `/data/local/tmp/.gl_restore`；恢复块目标 `733344 (0xB30A0)`；16 B 原厂值 `bf2303d5 c0035fd6 5f2403d5 280040f9` | `TypedBackendProbe.kt:938-950` |
| 探针门目标 | 优先 `/data/local/tmp/ghostlock-carrier`（可读可写且 ≥4096 B），否则 app tmp 下自建 16×0x5A 文件 | `:348-355` |
| 读取样偏移 | `0xFE600 / 0xFE5F0 / 0xFE610 / 0xFE5F8 / 0xFE608 / 0xFE5E0 / 0xFE620 / 0xFE5C0 / 0xFE640 / 0xFE680 / 0xFE700` | `:827-833` |
| 探针 wait 夹具 | budget 40 / poll 10；marker `ghostlock-ok/failed/running`；module `ghostlock`；模块名 `/data/local/tmp/ghostlock-wait-probe` | `:327-340` |
| SA 帧 | payload 84 B（+4 B 长度前缀），version 1、kind 1、icv 16、trailer `0x34383238` | `app/src/main/kotlin/com/ghostlock/app/jni/cve43284/SaFrame.kt:31-36` |

### 2.2 native 常量

| 常量 | 值 | 证据 |
|---|---|---|
| hook 计划（声明值，非运行期推导） | site file `0xB30AC` / vaddr `0xB70AC`；guard `0xD503245F`(bti c)；displaced `0xF9400028`；payload file `0xFE628` / vaddr `0x102628` / max `0x9D8=2520`；entry `0x90`；jump-back `0x1D4`；displaced slot `0x1D0` | `core/src/backend/cve_2026_43284/hook/sentry_hook.cpp:47-78`（vaddr/file delta 0x4000 注释 `:54`） |
| B 编码 | opcode `0x14000000`、imm26、±128 MiB、4 B 对齐 | `sentry_hook.cpp:12-39`、`:80-91`、`:113-119` |
| 载荷 blob | libcxx.S 472 B → 480；sha256 `d226d2e7…` | `core/src/backend/cve_2026_43284/embed/libcxx_blob.hpp:34-45` |
| helper blob | splicehelper 1432 B → 1440；sha256 `3c0a108e…` | `core/src/backend/cve_2026_43284/embed/splicehelper_blob.hpp:31-36` |
| 写单位/头/最大报文 | `kBlockBytes=16`、`kHeaderBytes=8`、`kMaxDatagramBytes=72` | `core/src/backend/cve_2026_43284/landing/pagecache_write.hpp:34-38` |
| 序号 | `open_doors` 重置为 1；发送成功后 +1；`adopt_sequence(0)→1` | `pagecache_write.hpp:116`、`:178-181`、`:237-239` |
| splice 失败码 | 1 参数 / 2 面不可用 / 3 太短 / 4 无页源 / 10 pipe / 20 头 / 21 页 / 22 helper 页 / 23 ICV / 24 短发；负值=-errno | `core/src/backend/cve_2026_43284/landing/real_splice_io.cpp:210-262` |
| 桥 | helper argv=`{crashdump64, offset, path}`、stderr/stdout 用管道、要求退出码 0 且整 16 B；路径上限 256 B、禁空白/控制符 | `core/src/backend/cve_2026_43284/landing/crash_dump_bridge.hpp:34`；`crash_dump_bridge.cpp:34-42`、`:86-99`、`:104-152` |
| 门/socket/面/落地/access 表 | 8 / 8 / 4 / 4 / 4；ticket=`slot+1`，打包票据 `(handle<<32) \| gen` | `core/src/jni/api_43284_device.cpp:55`、`:455`、`:500-502`、`:812`、`:561` |
| socket 失败码 | -1 无材料 / -2 socket / -3 bind / -4 connect / -9 表满 | `api_43284_device.cpp:392-440`、`:509-525` |
| resolve 打开方式 | `O_RDONLY \| O_CLOEXEC`；EACCES/EPERM ⇒ remote(fd=-1) 占槽 | `:320-331` |
| 块宽（JNI 层） | `kBlockWidth=16`；状态 `Empty/Dispatched/Applied/Verified` | `core/src/jni/api_43284.hpp:34`、`:63-68` |
| 逐块校验重试间隔 | `usleep(2000)`（2 ms）+ 每回合重发 | `core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:251-291` |
| 触发/wait 步的默认 | trigger 无 exec 路径；wait 轮询 `poll_ms \|\| 1`、模块路径 `/sys/module/<name>`（空则 dirtyfrag） | `pagecache_steps.cpp:152-175`、`:186-189` |

### 2.3 路径与标记（现状）

| 用途 | 路径 | 证据 |
|---|---|---|
| 帧 | `/data/local/tmp/ghostlock-sa.frame` | `TypedBackendProbe.kt:162`、`:219-222` |
| UMH 收尾脚本 | `/data/local/tmp/.ghostlock_lkm_cmd.sh` | `:558` |
| 模块 | `/data/local/tmp/ghostlock.ko` | `:971` |
| 载体 | `/vendor/lib64/libbinderdebug.so` | `:909`、`:973` |
| Access 线 marker | `/data/local/tmp/ghostlock-…`（success/failure/inflight/module）（默认 markerDir=`/data/local/tmp`） | `Assembled43284.kt:569`、`:587-590` |
| 探针 wait marker | `/dev/dfm0` / `/dev/df1` / `/dev/df` / `/sys/module/dirtyfrag` / 模块名 `kernelsu` | `TypedBackendProbe.kt:995-999` |
| 手动恢复 marker | `/data/local/tmp/.gl_restore` | `:942` |
| ksud 只读源 | `/sys/module/kernelsu`、`/data/adb/ksu*`、`/data/local/tmp/.ghostlock_lkm_{ok,fail}`、`/data/local/tmp/ghostlock-ksu.log` | `:1005-1021` |

## 3) 写管线（单位 / 对齐 / 校验 / restore / ESP 组帧与 ICV / 序号）

**单位**：16 B。JNI 层拒非 16（`api_43284.hpp:34`、`api_43284.cpp:217`）；Access 层只认 `kWriteWidth`（`core/src/backend/cve_2026_43284/kernel_access.hpp:109-111`、`:146-148`）；窗口单位 `kBlockBytes`（`pagecache_write.hpp:34`）；写 hook 传 8 会被 `BadWidth` 拒（`docs/analysis/device-gates/43284-hook-write-verified.md:1`）。

**对齐**：只有 `write_hook` 做块对齐——`block_at = site - site % width`，并要求 `within ≥ 4`、`[within-4]`=`guard`、`[within]`=`displaced`（`pagecache_steps.cpp:81-120`）。`write_image_verified` **不做任何对齐/边界检查**：从 `request.offset` 起按 width 推进，最后一块零填充（`:222-241`）；退役链有 plan 闭合（region 不重叠、落在 plan.extent / 载体声明 size / fstat 真实 size 内）与 `TargetOutOfBounds`（`6f7ded87:src/core/backend/cve_2026_43284/steps/chain.cpp:299-344`、`stage_runner.cpp:980-993`；`docs/analysis/retired-chain-inventory.md:80`）。当前无 preimage 声明比对（退役链有 `PreImageMismatch`）。

**校验**：唯一校验 = 写后**经同一 Access/落地回读**并 `memcmp`（`pagecache_steps.cpp:262-272`）；不符则在 `budget_ms` 内**有界重试**（每回合先重发再回读，间隔 2 ms），预算用尽报 `step-verify-failed` 并打印 `want/got`（`:273-288`）。**短读不参与比较**（`got unreadable` 分支 `:280-281`）。JNI 侧的状态机另有一层"Applied"语义：`write_block` ⇒ `Dispatched`（`api_43284.cpp:210-232`），只有同偏移 `read_block` 比对一致才 `Applied`（`api_43284.cpp:236-268`）。

**restore / 回滚**：**当前链内没有任何自动回滚或 hook 恢复**（无 journal、无 `restore_hook_patch` 对应物）；只有探针里一条 **marker 门控的手动恢复**（`.gl_restore` 存在时把 0xB30A0 的 16 B 原厂值写回），且它**只在载荷写成功那条分支里**执行（`TypedBackendProbe.kt:938-950`）。退役链：有界 128 块 journal、失败时"新→旧"回滚、**每条终结点**都 `restore_hook_patch`（arc 先 trampoline 后 shellcode），`release` 恰一次 + 擦 journal（`6f7ded87:steps/chain.hpp:32-58`、`steps/chain.cpp:136-153`、`:273-293`、`steps/hook_patch.cpp:193-220`）。

**ESP 组帧与 ICV**：`SPI(4 BE) | Seq(4 BE) | IV(16) | ciphertext(16) | ICV(icv_len)`；内存里拼前缀 `8+16+16=40`，密文槽**由 real I/O 在发送时从目标页 splice**（或由 helper 直接 splice 进管道）；ICV=HMAC-SHA256(hmac_key, 40 B 前缀) 截断到 `icv_len=16`；整报文 `40+16=56` B；ICV 按**页里现有字节**算（不是期望块），与退役链同因（`pagecache_write.hpp:145-171`、`esp_crypto.hpp:59-74`、`real_splice_io.cpp:235-256`、`docs/analysis/device-gates/43284-native-access-20261010-write-pass.md:23-24`）。IV=`AES256_ECB_DEC(K, old) XOR desired`（`esp_crypto.hpp:37-52`）。旧块来源优先级：直接 `pread(fd)` → 失败且绑了桥则 helper 读（只收整 16 B，否则 fail-closed 不发电文）（`real_splice_io.cpp:166-208`、`:236-252`）。

**序号（现状做法）**：窗口 `open_doors` 一律从 1 起（`pagecache_write.hpp:116`），发送成功后 +1（`:178-181`）。跨窗口接续在当前树上靠**显式两件套**：Kotlin 读 `nextSequence()` 再调 `adoptSequence(next)`（`Assembled43284.kt:382-396`；探针 `TypedBackendProbe.kt:813`、`:897`；native `api_43284_device.cpp:753-775`）；`Init43284.initialSequence/sequencePresent` 这条**参数通路当前是断的**（见 M3）。另外 `writeBlock` 这条路每次调用都会把窗口序号改成 `slot->sequence+1`（`api_43284_device.cpp:246-252`），slot 序号从 0 起 ⇒ **换一张票据（新门）就从 1 重新开始**——这正是"两条链各自从 seq=1 起"的结构性来源（`docs/analysis/device-gates/43284-hook-write-verified.md:1`）。

## 4) 触发与等待

**trigger 形态**：`steps::trigger_orphan(delay)`＝`fork()` → 中间子进程立刻 `_exit(0)` → 孙子 `usleep(delay*1000)` 后 `_exit(0)`（**无 exec**；参考实现另有可选 `exec_path`）；父进程 `waitpid`（EINTR 重试）（`pagecache_steps.cpp:152-175`；退役侧 `6f7ded87:src/core/backend/cve_2026_43284/real_ops.cpp:346-389`）。探针用 `delay=500`（`Assembled43284.kt:585`；fallback 2000，`TypedBackendProbe.kt:989`）；退役分阶段入口同 500（`stage_runner.cpp:999`），但 profile 默认是 1000（`procedures/pagecache_load_lkm.lua:31-33`）。

**wait 判据**：`wait_for_module` 顺序＝① 失败 marker 存在 ⇒ `step-wait-failure-marker`；② 成功 marker ⇒ 成功；③ `/sys/module/<module_name>`（或 module_dir）曾出现、现在消失 ⇒ `step-wait-module-gone`；④ 预算用尽 ⇒ `step-wait-timeout`（`pagecache_steps.cpp:177-208`）。native 映射：`step-wait-timeout ⇒ InFlight`，其余非空 ⇒ `Failure`（`api_43284_device.cpp:196-203`）。探针参数：`budget 15000 / poll 100`，marker `/dev/dfm0 \| df1 \| df`，模块名 `kernelsu`（`TypedBackendProbe.kt:992-1000`）；退役侧是 `poll 10 / budget 5000`、`/dev/dfm1 \| dfm0 \| df` + `/sys/module/dirtyfrag`（`6f7ded87:real_ops.hpp:56-63`、`real_ops.cpp:391-432`；`stage_runner.cpp:860`）。

**标记文件怎么读**：native 只做**存在性判定**（`stat`），**不读内容**（`api_43284_device.cpp:134-137`、`:187-195`）；内容读取只在 Kotlin 侧：`ghostlock-ksu.log` 取尾 3 行、`/proc/net/xfrm_stat` 整行、`crash_dump64` 前 48 B（`TypedBackendProbe.kt:1016-1021`、`:755-760`、`:732-737`）。
**注**：探针传的 `moduleMarker=/sys/module/dirtyfrag` 在 native 侧**没有被接进 `WaitRequest.module_dir`**（`api_43284_device.cpp:187-195` 只映射 success/failure/in_flight/module_name）⇒ "present→gone" 这条规则实际只由 `/sys/module/kernelsu` 触发；退役链的 dirtyfrag 语义在设备链路上丢了。

## 5) 与成功历史相比缺什么（missing 清单）

对照 = 退役链 `6f7ded87`（PASS）＋对面清单 `docs/analysis/retired-chain-inventory.md`。

| # | 缺什么 | 退役侧证据 | 当前侧证据/现状 |
|---|---|---|---|
| M1 | **无自动回滚/恢复**：没有 journal（128 块、"新→旧"）、没有 `restore_hook_patch`、没有"每条终结点"顺序 | `6f7ded87:steps/chain.hpp:32-58`、`steps/chain.cpp:136-153`、`:273-293`、`steps/hook_patch.cpp:193-220` | 链内无任何 restore 代码；只有探针的 marker 门控手动恢复（`TypedBackendProbe.kt:938-950`），且只在载荷写成功分支 |
| M2 | **hook 计划不是运行期推导**：无 ELF 解析/符号查找/落位计算，换镜像就失效 | `6f7ded87:steps/elf_hook.cpp:249-331`、`:412-414` | `sentry_hook.cpp:47-78` 常量声明；写-pass 记录自认"未移植"（`docs/analysis/device-gates/43284-native-access-20261010-write-pass.md:77`） |
| M3 | **序号参数通路断了**：`device_access_initialize` 只拷 `params.sequence`，**没拷 `params.sequence_set`** ⇒ `has_sequence()` 假 ⇒ `Access::initialize` 不 adopt | `6f7ded87:real_ops.cpp:303-307/321-325/336-342`；修法记录 `docs/analysis/device-gates/43284-hook-write-verified.md:1` | `api_43284_device.cpp:895`（只有 sequence 一行）；`access_params.hpp:161`；`kernel_access.hpp:76-82`。该行在 `893fce97` 的重排里被吃掉（`git show 893fce97 -- core/src/jni/api_43284_device.cpp`：旧行 `params.sequence_set = sa_source.sequencePresent;` 随替换块消失） |
| M4 | **载体回退表缺失**：只有写死的 `/vendor/lib64/libbinderdebug.so`，没有 4 载体按上游顺序的尝试/去重/vendor 前缀校验/dev 旁路 | `6f7ded87:steps/chain.hpp:70-75`、`steps/chain.cpp:19-46/48-77` | `TypedBackendProbe.kt:909`、`:973`；`Assembled43284.kt:567-591` 只带一个路径 |
| M5 | **模块预检没进链**：真实 `.ko` 的 vermagic/KMI 预检只在工具里做；探针喂假镜像，链上不做 | `6f7ded87:stage_runner.cpp:98-146`、`backend_terminal.cpp:114-127` | `api_43284_device.cpp:148-160`（能力在）＋`TypedBackendProbe.kt:318-323`（假字节）；`Assembled43284.kt:580-582` 三个保守标志 |
| M6 | **生产/终局契约缺失**：设备事实采集、KMI 选择、UMH late-load 命令、secrets 借出、root 判据（uid=0 / Enforcing / modules 含 kernelsu）都没进执行路径 | `6f7ded87:backend_terminal.cpp:38-156`；判据 `docs/analysis/device-gates/43284-terminal-criteria-verified.md:1` | 只有 `deployUmhPayload` + ksud 只读状态（`TypedBackendProbe.kt:556-569`、`:1002-1027`） |
| M7 | **wait/trigger 参数与判据不一致**：poll 100 vs 10、budget 15000 vs 5000、`moduleMarker` 未接线、模块名 `kernelsu` vs `dirtyfrag` | `6f7ded87:real_ops.hpp:56-63`、`stage_runner.cpp:860` | `TypedBackendProbe.kt:992-1000`；`api_43284_device.cpp:187-195` |
| M8 | **写前边界/plan 闭合缺失**：无 `fstat` 尺寸断言、无 region 重叠检查、无 `TargetOutOfBounds`/`PreImageMismatch` | `6f7ded87:steps/chain.cpp:299-344`、`stage_runner.cpp:980-993` | `pagecache_steps.cpp:209-241` 直接按 offset/width 走；`api_43284_device.cpp:935-1048` 无边界检查 |
| M9 | **hook 写入顺序相反**：native `write_hook` 先站点后载荷；参考先载荷（shellcode）后 trampoline | `6f7ded87:steps/hook_patch.cpp:176-190`、`docs/analysis/retired-chain-inventory.md:43` | `pagecache_steps.cpp:121-150`；探针绕开它自己按"载荷→站点"（`TypedBackendProbe.kt:911`→`:915`） |
| M10 | **槽内容/立即数与退役基准不同**：mutex `/dev/dx` vs `/dev/df`；selinux 长度立即数 `len` vs `len+1`（含 NUL） | `6f7ded87:steps/hook_patch.hpp:146`、`steps/hook_patch.cpp:302-311` | `Assembled43284.kt:183`（有注释说明理由）、`:218`（**少 1**，可能让写 ctx 长度不等上游） |
| M11 | **patch1 的独立校验语义**：退役链写完后校验 offset 16 的 16 B、短读按"verify skipped"放行；当前是整镜像逐块回读，无"skip"档 | `6f7ded87:steps/crash_dump.cpp:52-65` | `pagecache_steps.cpp:209-294`（全块比对） |
| M12 | **探针顺序与参考顺序不同**：hook 早于 carrier 写 | `6f7ded87:steps/chain.hpp:107-121`、`docs/analysis/retired-chain-inventory.md:33` | `TypedBackendProbe.kt:803`（① hook）先于 `:969`（② module） |
| M13 | **生产配置未接**：profile 键存在但链不读（延时/预算/轮询/块宽都是 Kotlin 常量） | `6f7ded87` 由 profile→stage 驱动（`backend_terminal.cpp`） | `profile/profile-schema.json:127-146` vs `TypedBackendProbe.kt:985-999`、`Assembled43284.kt:485-585` |

（不算缺口的既有能力，供对拍：两桥已移植——`crash_dump_bridge.cpp:64-153` 的 `{offset,path}` 调用约定与 `real_splice_io.cpp:236-252` 的 helper splice；逐块写+回读；ESP 组帧/IV/ICV；门→拼接面→落地→Access 装配；double-fork 触发；marker 轮询。）

## 6) 已知的现场失败（本会话真机看到的具名失败）

> 现场 logcat 原文未入库（仓库内无 logcat 文件；app file-sink 也没找到落盘副本），所以每条给：**现象（现场行原文）** + **产生该行的代码** + **已入库的同类记录**。凡只有现场 logcat、没有入库记录的，标 `[仅现场]`。

| 现象（现场行） | 产生该行的代码 | 入库记录 / 判读 |
|---|---|---|
| `image-write:step-verify-failed` | `core/src/jni/native_43284.cpp:564`（`"image-write:"+why`）× `steps/pagecache_steps.cpp:25`（`kVerifyFailed`）／`:286` | `docs/analysis/j7-5-handoff.md:31`（`patch1 wrote=false refusal=image-write:step-verify-failed`）——第 ⑤ 步回读比较没过 |
| `want 0100b700 got 0300b700` | `steps/pagecache_steps.cpp:274-278`（`block[%zu] verify failed: want … got …`） | 真因口径：`0100`=ET_REL（模块）、`0300`=ET_DYN（动态库）；**offset 0 的比较是假阳性**，真判据在 `0x10` 的 `e_type` — `docs/analysis/device-gates/43284-native-access-20261010-write-pass.md:48` |
| `helper splice spliced=16 head=0300b700` | `core/src/backend/cve_2026_43284/landing/real_splice_io.cpp:246-250`（helper 送进管道的块数 + 同一来源读回的前 4 B） | 读到的头 4 B=`0300b700` ⇒ 该偏移处是 **ET_DYN**（`.so`），即"目标/偏移指到了动态库那侧"；helper 侧 splice 本身返回了整 16 B（spliced=16） |
| `read_block fd=-1 pread=-1` | `core/src/backend/cve_2026_43284/landing/pagecache_write.hpp:204-217`（诊断：`pread` 与 `fcntl/readlink`） | 语义：remote 载体（vendor）本进程无 fd，直接 pread 必然 `-1/EBADF`；**这一行不是失败判据**，是"读法/来源"的分辨器（`api_43284_device.cpp:321-331` 同源说明） |
| `landing_of_door` 命中面（hit/ambiguous/miss） | `core/src/jni/api_43284_device.cpp:686`(hit) / `:692`(ambiguous) / `:695`(miss) | 命错面的真实代价：Access 落到载体的 landing ⇒ 读零/写错目标/verify 必败 — `TypedBackendProbe.kt:694-695`；修法（打包票据优先 + 槽值唯一命中）在 `fff339d0`/`70d33798` 系提交 |
| 两条 seq 链**现已连续** `91→…` | `TypedBackendProbe.kt:813`、`:897`（`adoptSequence(stepwise.nextSequence())`）/`:819`；native `api_43284_device.cpp:765-775`；窗口 `pagecache_write.hpp:237-239` | `docs/analysis/device-gates/43284-hook-write-verified.md:1`（`nextSequence before hook=91` → `adopt seq=91 present=1` → `hook wrote=true`）；历史失败形是"hook 窗口从 seq=1 起 ⇒ 报文被防重放丢弃"（同文件 `:1`） |
| 站点块 `verified=true` | `TypedBackendProbe.kt:958`（`hook site block wrote=… afterWord=0x… verified=(afterWord==word2)`） | 与它成对的是同段的 `site block before=`（`:932`）与"分支词取 `plan[0]`"（`:928-929`）；成功历史的同形证据 `docs/analysis/device-gates/43284-native-access-20261010-write-pass.md:62`（`phase 5 site ok, 0x14012d83 at 0xb30ac`） |

`[仅现场]` 项：上表第 2、3 行的具体 want/got 与 head 值、第 5 行的命中面分支、第 7 行的 `verified=true` 只有本会话 logcat 读数；仓库里能核的是**产生这些行的代码**与同形历史记录（已在表内逐条给出）。

## 7) 口径与不确定项

- 探针是"Kotlin 拼装 + JNI 薄原语"两条路混用：patch1 同时存在 `stepwise.writeHelperThroughLanding`（landBlock，无校验）与 `patcher.writeImage`（逐块回读）两次写；账要按两条都算（`TypedBackendProbe.kt:628`、`:678`）。
- `api_43284_device.cpp:901-904` 里的 session landing self bring-up 是"resolve 没材料、initialize 才带材料"的补丁路径，与 `g_surfaces/g_landings` 那套并不共享同一对象——两套落地同时存在，序号与读面归属要按日志分辨（`Landing43284` vs `Surface`，`:115-132` vs `:484-502`）。
- 本文件的所有"当前侧"行号取自 HEAD `08b08cd3` 的**工作树**（含未提交改动之外的内容）；Kotlin 侧行号取自工作树当前字节。
