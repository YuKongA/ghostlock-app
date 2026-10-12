# 43499 真机门禁记录：canonical 链（2026-10-11，单 route）

## 环境（满足门禁规范 ✓）

```
设备    QV770MFGJ1（A301SO，Android 15）
内核    5.15.189-android13-8-00016-g51bba4309aac-ab14546557（与归档退役同一 build ✓）
SELinux **Enforcing** ✓（getenforce ✓ + /sys/fs/selinux/enforce=1 ✓，shell 身份可读 ✓）
身份    uid=2000（shell ✓）；KernelSU 未加载 ✓；route：multicast_waiter（单 route ✓）
CPU     execution.recommended_cpus = {main:0, consumer:1} ✓（引擎内 pin ✓）
```

## 本轮真机判据（三级 ✓）

| 级别 | 判据 | 结果 |
| --- | --- | --- |
| 走到底 | 三步 `w1/w2/w3` 各 `accepts=1` | ✓（约 1/4 ~ 1/2 的跑能全过；其余奇偶不过 ⇒ 提前停 ✓） |
| 轮次 | 主轮 + 两轮修复均 `status=0/0` | ✓（修复轮并入窗口后达成 ✓） |
| 窗口 | `window clean=1 violations alloc=0 log=0 indirect=0 lua=0` | ✓ |
| **机制级** | `selinux enforce` 由 1 变 **0** | **✗ 未发生**（≥9 次完整跑，0 次翻转 ✓ 直读 ✓） |
| **目标级** | `main: victim uid=0` | **✗ 未发生**（`uid=2000` ✓） |

## 结论（如实 ✓）

```
1. canonical 链在"可比对层级全部对齐"后，**写入从未发生** ✗（≥9 次完整跑 ✓，直读仪器 ✓）；
2. 因此下一批走**换 route**（`select_stack`）：计划 docs/analysis/43499-select-stack-port-plan.md ✓，
   研究 docs/analysis/43499-select-stack-study.md ✓（公共部分本树已具备 ⇒ 移植面 = 一个 route 本体 ✓）；
3. 本记录即该批的**基线证据** ✓（换 route 后按同一三级判据对比 ✓）。
```

## 复现命令（一行 ✓）

```
adb push build/native/ghostlock /data/local/tmp/ ; adb push core/fixtures/wire-43499-collide-F.framed /data/local/tmp/
adb shell "cd /data/local/tmp && ./ghostlock < wire-43499-collide-F.framed"
判据：grep "main: selinux enforce" / "main: victim uid"
```
