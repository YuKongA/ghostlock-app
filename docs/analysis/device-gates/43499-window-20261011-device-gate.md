# 43499 窗口三步真机门禁（2026-10-11 · 第二版）

## 判据（要什么算过）

- `w1` / `w2` / `w3` 在设备上各返回 **won**（阶段返回 nullptr）✓；
- 引擎报告 `status.code == 0` 且 `attempts > 0`（真跑过，不是空转）✓；
- 整链**无具名拒绝**（`main: verdict none`）✓；
- 载荷页的指针与退役布局**逐项吻合**（`fake_task − page = 0x1280`、`fake_lock − page = 0x0e80`）✓；
- 目标**尚未**达成的项：`after uid=` 仍为 2000（拿 root 在读面批次 ✓）。

## 方法（可复现）

- 设备 `QV770MFGJ1`（A301SO，Android 15，`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`）；
- `make -C core -B android NDK_TOOLCHAIN=<ndk>/toolchains/llvm/prebuilt/darwin-x86_64/bin` ⇒ `adb push build/native/ghostlock /data/local/tmp/`；
- `adb push core/fixtures/wire-43499-collide-F.framed /data/local/tmp/`（**夹具要与设备同步** —— 本轮踩过一次旧副本 ✓）；
- `adb shell 'cd /data/local/tmp && ./ghostlock < wire-43499-collide-F.framed'`；
- 前置：KernelSU 未加载（本链以 shell 身份跑 ✓）。

## 原始输出（逐字，2026-10-11 晚）

```
main: payload page at 0xffff80000006d800 fake_task=0xffff80000006ea80 fake_lock=0xffff80000006e680
main: route status=0/0 attempts=2 calls=4 successes=4 stamp=-1/99 requeue=0 waiter=-1 owner=0
main: step result w1 => won
main: step result w2 => won
main: step result w3 => won
main: prepare status=ok step=11 opened=6 closed=7 base=0xffff8000000e8000 errno=0
main: discovery verdict=ok probes=176 rounds=1
main: after uid=2000 euid=2000 gid=2000 egid=2000
main: verdict none
```

## 边界（这份记录**不**声称的事）

- `stamp=-1 errno=99`（EADDRNOTAVAIL）**正是预期**（退役原话："still means the waiter shape is on the stack now"）✓，
  判赢看 `successes`（`consumer_success > 0`）✓ —— 但**写是否落到目标**本记录不作主张 ✗；
- **"页内字节不变"不能证明写没发生** ✗：设计里没有一步往载荷页写（内核只**读**它）✓ ⇒ 哨兵判据已作废 ✓；
- 最终判据只有一个：**`after uid=` 由 2000 变 0** ✓（依赖读面批次 ✓）。

## 时间线

- 首版：三步 won（同日早些时候 ✓，已并入本版）；
- 本版：载荷页进内核域 + 指针与退役布局逐项吻合 + 判据纠正（哨兵作废 ✓）；
- 门禁：`check` 112 PASS、`lint` 0、`android -B` 0 警 ✓。
