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
