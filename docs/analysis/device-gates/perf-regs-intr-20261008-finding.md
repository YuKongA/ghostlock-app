# 真机发现（已修正）：perf 探针 sample_type 用错 WEIGHT 位 ⇒ 24B 无 regs（2026-10-08 原始记录）

> 级别：**攻击关键路径相关**（43499 写原语 `perf_find_task` 依赖它）⇒ 按 AGENTS 归档 ✓
> 设备 `QV770MFGJ1` · `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`
> 探针：`tools/perf_ops_probe.cpp`（`make -C core perf-landing-probe`）
>
> **2026-10-27 修正（task-46/B1）**：本 finding 的探针把 `PERF_SAMPLE_REGS_INTR`
> 写成了 `1<<14`（`PERF_SAMPLE_WEIGHT`），正确为 **`1<<18`**；因此只看请求位就
> 已经决定了内核 **不会投递寄存器载荷**（记录=头8+IP8+ABI8=24B，ABI=0）。
> **原结论「寄存器组在这台设备上不投递」撤回**；§1 原始输出保留（事实不变），
> §4 给出修正后的重跑/判据。修正依据：NDK `linux/perf_event.h`
> `PERF_SAMPLE_WEIGHT=1U<<14`、`PERF_SAMPLE_REGS_INTR=1U<<18`；legacy 用内核宏
> `PERF_SAMPLE_IP|PERF_SAMPLE_REGS_INTR` = 262145；同内核 10-06 日志有 149/162 票
> 寄存器候选（`20261006-015621:99-108,146-147`）。

## 1 事实（2026-10-08 原始输出，原样保留）

```
perf landing probe
  perf_open            fd=3 errno=0        <- 采样可以开（未被 paranoid 挡住）
  perf_map             ok
  ring                 offset=4096 size=32768
  data_head after sampling 32760           <- 采样器确实写了满环记录
  rec[0] type=9 size=24 bytes=09 00 00 00 01 00 18 00
        abi=0 payload=                     <- ABI=0 且无载荷
  ring walk            samples=0 kept=0    <- 因此按规则不产生候选（拒绝正确）
```

## 2 原结论（2026-10-08）— 已撤回

> ~~记录 = 头(8) + IP(8) + ABI(8) = 24 字节 ⇒ 请求的寄存器组没有随样本投递。~~

**修正**：24B/ABI=0 是探针 `sample_type=1<<14`（WEIGHT）的**必然结果**，不是内核
不投递、也不是权限/paranoid 问题（同段原始输出已显示 open 成功、满环写入）。
请求 `sample_type=262145`（IP|REGS_INTR）时内核按 `sample_regs_intr` 位图投递
寄存器，记录应为 **280B**（8+8+8+32×8，ABI=1）。本机 262145 的寄存器投递
**尚待 G1 真机复跑**（UNVERIFIED）。

## 3 仍然成立的部分

- 本轮**读取器与投票规则正确**：记录里没有可用寄存器 ⇒ `samples=0` ⇒ 投票 0 ⇒
  拒绝路径在真机上被验证了一次；
- 移植时必须**具名报出**（`perf-no-samples` / `perf-registers-not-delivered` /
  `perf-record-invalid`），不得静默返回 0；
- `abi=0` 24B 记录在实现中被显式计为 no-registers（附件 A §B），不会误投票。

## 4 修正后的重跑（G1 用）

```sh
# 探针必须用内核宏：PERF_SAMPLE_IP | PERF_SAMPLE_REGS_INTR = 262145
# （禁止 16385 / 1<<14；实现侧单一常量见 Cve43499PerfSampleFormat）
make -C core perf-landing-probe
adb -s QV770MFGJ1 push build/native/perf_ops_probe /data/local/tmp/
adb -s QV770MFGJ1 shell 'timeout 25 /data/local/tmp/perf_ops_probe'
```

判据：记录 `size=280`、`abi=1`、32 regs（含 `sp=reg31`）⇒ 投递成立；若仍 24B/ABI=0
或 open 失败，才按实现原因具名拒绝并记录 `perf_event_paranoid`/上下文/跨任务属性。

## 5 复现（历史）

```sh
make -C core perf-landing-probe
adb -s QV770MFGJ1 push build/native/perf_ops_probe /data/local/tmp/
adb -s QV770MFGJ1 shell 'timeout 25 /data/local/tmp/perf_ops_probe'
```
