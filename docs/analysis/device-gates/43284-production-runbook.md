# 43284 生产级冷跑 runbook（可重复验收）

> 目的：任何人都能按本文在**冷启动后的干净设备**上，用临时 UI 的**同一个按钮**完成一次 43284 全链，
> 并按“判据表”判定成功或具名失败。43499 不在本文范围（它将有自己的按钮）。

## 0. 前置（缺一不可）

| 前置 | 如何确认 | 失败时的现象 |
|---|---|---|
| 冷启动（本会话未加载过模块） | `grep -ci kernelsu /proc/modules` ⇒ `0` | 按钮立即 `skip: kernelsu already loaded` |
| 站点为原厂字 | `dd if=/system/lib64/libc++.so bs=1 skip=733356 count=4 \| od -An -tx1` ⇒ `bf 23 03 d5` | 若显示 `83 2d 01 14`，说明页里还留着上轮钩子 ⇒ 需冷启动 |
| UMH 脚本已部署（新版 103 行） | `wc -l /data/local/tmp/.ghostlock_lkm_cmd.sh` ⇒ `103` | 旧版 81 行 ⇒ 旧脚本，缺 `post-fs-data/services/boot-completed` |
| 应用已安装且临时 UI 在 | `pm path com.ghostlock.app` 有输出 | — |

## 1. 操作（只有一步）

1. 启动临时 UI：`adb shell monkey -p com.ghostlock.app -c android.intent.category.LAUNCHER 1`
2. 点「★ 完整攻击（43284 …）」这一个按钮（屏幕坐标以面板布局为准；本轮为 `548 265`）。
3. 观察**统一日志面板**（它显示的与 `files/ghostlock-run.log` 逐行一致）。

## 2. 判据表（按出现顺序）

| 阶段 | 期望（统一日志里的行） | 含义 |
|---|---|---|
| 部署 | `umh script deployed bytes=… sha=…` | 新版脚本已落盘 |
| SA | `ipsec sa built transform=true retainedAesNonZero=32 retainedHmacNonZero=32`，并打印 `spi=0x… encap=… sender=…` | 真实材料就绪；**这行必须先于任何 probe 行出现** |
| patch#1 | `patch1 wrote=true helperBytes=1440`（90 块，逐块 `wrote=16/16`） | crash_dump64 的 helper 就位 |
| 载体 | `module bytes=12304` ⇒ `module wrote=true`（**769 块**，逐块回读一致） | 与退役 `written=769 verified=769` 等价 |
| 钩子 | `hook site block wrote=true afterWord=0x14012d83 verified=true`；`payload wrote=true` | 站点=我们的分支、载荷=480 B |
| 触发 | `hook trigger code=0 delayMs=500 used=500` | double-fork 孤儿已发出 |
| 等待 | `hook wait terminal=1`（成功）**或** `terminal=2`（失败/超时） | terminal=2 时立刻看 `wait_for_module -> <原因>` |
| 标记 | `/dev/dfm0` 存在（`/dev/df` 亦在），`/dev/dfm1` 不存在 | app 侧只可 stat，不可读（`Permission denied` 即“存在”） |
| 内核 | `grep -i kernelsu /proc/modules` ⇒ `kernelsu <size> 1 - Live …` | 模块在内核 |
| 用户侧 | `su -c id` ⇒ `uid=0(root) … context=u:r:ksu:s0`；`getenforce` ⇒ `Enforcing` | 根已可用 |
| UMH | `tail -8 /data/local/tmp/.ghostlock_lkm.log` 含：`late-load exit=0` · `post-fs-data exit=0` · `boot-completed exit=0` · `ksu dir=present` · `[+] KernelSU module loaded` | ksud 常驻侧就绪 ⇒ **管理器显示“已加载”** |

## 3. 具名失败与处置（全部已在真机见过）

| 现象 | 根因（已定位） | 处置 |
|---|---|---|
| `saInit: SA material missing -> fallback to probe material` | SA 尚未建立，材料退化 | 让 SA 区先跑；未就绪时链入口硬失败 `sa-material-missing` |
| `window: doors open spi=0x00001234` | 报文用了测试 SPI，内核静默丢弃 | 所有会发报文的窗口都必须用 `saInit(...)` |
| `landing_of_door miss token=0x7 want=6 surfaces=4` | 门与面不匹配 | 建面按门的 ticket 注册；不匹配要显式报错 |
| `patch1 init=false refusal=access:no-handle` | 上游：无 access 句柄（多为 SA/面未就绪的连锁） | 先修 SA 与面 |
| `block[16] verify failed: want … got …`（且 `helper splice spliced=16`） | 读侧自洽、写未落地 | 查该窗口的 SPI/序号/材料 |
| `hook wait terminal=2` + `step-wait-timeout` | 三标记都未出现 | 载荷未被踩或域不对；查 `detonate` 与钩子是否在页里 |

## 4. 收尾（临时物清单，跑完请清零）

```text
/dev/df /dev/dfm0 /dev/dfm1            # 三标记（root 可删）
/data/local/tmp/.ghostlock_lkm_ok .ghostlock_lkm_fail
/data/local/tmp/.ghostlock_lkm.log     # 历史，保留便于比对
```

## 5. 复核命令（一次性粘）

```text
adb -s QV770MFGJ1 shell 'grep -i kernelsu /proc/modules; ls -l /dev/df /dev/dfm0 /dev/dfm1; su -c id; getenforce; tail -8 /data/local/tmp/.ghostlock_lkm.log'
adb -s QV770MFGJ1 shell 'run-as com.ghostlock.app tail -20 files/ghostlock-run.log'
```

## 6. 速度基线（2026-10-16 晚，对齐 `ghostlock-app-ref@5f184760`）

| 项 | 参照实现 | 我们（本批之后） | 依据 |
|---|---|---|---|
| 载体 | 可直读（每块 pread，约 3.4 ms/块） | 已改 `/system/lib64/libc++.so`（可直读） | `4ce37fa7` · ref `pagecache.cpp:158-165` |
| 重试 | 每块只读一次 | `imageBudgetMs = 0`（不重试） | `ddda1931` · ref `chain.cpp:321` |
| 块数 | 1470 块（23520 B 模块） | 769 块（12304 B 模块） | 记录 `written=1470` vs `module bytes=12304` |
| 期望耗时 | 约 5 s | **约 2.6 s**（769 × 3.4 ms） | 本表换算 |

## 7. 模块 vermagic（跨机型）

模块只在 `vermagic` 等于本机 `UTS_RELEASE` + 选项串时才可 `insmod`；同一 minor 布局一致，故可**原地改名**：

```text
公式（ref lkm_image.cpp:388-412）：<release> SMP [preempt] [mod_unload] [modversions] aarch64
实现：Kotlin retagVermagicForDevice()（2c588a82）；放不下就原样返回并记一行，绝不截断 ELF。
```

## 8. 已加载态下仍可重跑

`kernelsu` 已在 `/proc/modules` 时，**不再跳过**：只打印 `note: kernelsu already loaded -> running anyway` 然后照跑
（`00b52132`）。依据：参照实现在已加载态依旧跑完并再次启动 ksu。

## 9. 收尾（restore）

参照实现在**唯一边界**无条件恢复钩子（`ghostlock-app-ref@5f184760:chain.cpp:350-359`：It runs on every terminus path），
并以 `hook_restored=1` 记录。我们已有 `steps::restore_hook`（幂等 + 单测）；接线（JNI + Kotlin try/finally）在 `18ca836e` 批次进行中。

## 10. vermagic 写回的契约（与 ref 逐条对照）

| 步骤 | 参照实现 `lkm_image.cpp:574-620` | 我们（Kotlin `retagVermagic`） | 等价 |
|---|---|---|---|
| 前置校验 | 校验 ELF 信封 + 解析 `.modinfo` | 直接搜首个 `vermagic=` | 近似（风险极低） |
| 放不下时 | `VermagicSlotTooSmall`，**镜像字节不变** | 原样返回 + 记一行 | ✅ 同 |
| 写入 | 写 `vermagic=` + 新串 + **槽内清零** | 同样 | ✅ 同 |
| 事务性 | 一切可能失败的判断都在第一次写入之前 | 同样（先算容量再写） | ✅ 同 |

结论：同一 minor 直接改名这一条**已对齐**；跨 minor 也不会破坏镜像（放不下就不改）。

## 11. 真机证据表（2026-10-16 晚，同一设备、同一构建线）

| 时间 | 模块 | 载体 | 结果 | 结论 |
|---|---|---|---|---|
| 22:02 | 12304 B | `/vendor/lib64/libbinderdebug.so` | `module wrote=true` | 写链可用 |
| 23:26 | 12304 B | `/system/lib64/libc++.so`（试验） | `step-verify-failed` | 不能把被钩的文件当载体 |
| 23:33 | 12304 B | vendor | `module wrote=true` · 载荷建出 `/dev/df` · **无 UMH** | 钩子与载荷正常，断点在 `insmod` |
| 23:43 | 23512 B | vendor | `image-write:step-write-refused:**call-failed**` | 该载体**收不下** 23512 B |

### 运行后的稳定现象（每轮一致）

```text
hook site block wrote=true verified=true    站点写入 + 回读 ✓
hook trigger code=0 delayMs=500             载荷被踩到 ✓
/dev/df 出现（shell 视角 Permission denied） 载荷执行到建互斥 ✓
UMH 日志停在 20:46                           模块**未进入内核** ✗
hook restore=true                           钩子被撤 ✓（不再遗留）
wait terminal=2                             因无 .ghostlock_lkm_ok ⇒ 超时（判据正确）
```

### 下一步的定性实验

把模块换成参照实现自带的 `dirtyfrag-13-5.15.ko`（11168 B）：比 12304 B 更小（必写得进去）且是 ref 自己在用。
它能载入 ⇒ 问题在我们的 `.ko`；它也载不进去 ⇒ 问题在载荷侧 `insmod`（域 / argv / 路径）。一次实验即可分开两类原因。
