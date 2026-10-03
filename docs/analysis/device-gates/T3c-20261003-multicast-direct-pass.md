# T3c 真机门禁：profile 语义迁移（recommend_shizuku→backend.steps、HOCON 布尔、C++ bool）— PASS（3 次冷机 2/1）

对应提交 `1af39d6`。候选二进制 `build/native/ghostlock` sha256
`bda198272b5fa9b6141ea0e4f449e47c9c867471e6b23839863aa7fbde303d5f`；
导出 profile `build/kernel-profiles/5.15.189-android13-8-00016-g51bba4309aac-ab14546557.bin` sha256
`8aa3200035b3768100cdf660be3b5a5a62b18936c4f653c35ee8a9243bb1b7af`（经 `./gradlew exportKernelProfiles --offline`，exit 0）。

## 设备与入口

- A301SO / `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`；serial `QV770MFGJ1`；route = multicast_waiter。
- 每次冷机：reboot → `sys.boot_completed=1` → sleep 8；post-boot `/proc/modules` 无 kernelsu。

## 结果（3 次冷机）

| 运行 | 结果 | boot_ms | route | child is root | KernelSU | panic |
|---|---|---|---|---|---|---|
| 1 | PASS | 34144 | 4× clean | ✅ | ✅ | 无 |
| 1b | FAIL | 33974 | 0× | ❌ | ❌ | 无（非 panic，设备未重启）|
| 2 | PASS | 32430 | 4× clean | ✅ | ✅ | 无 |

Pass 样本（`.native.log` = run 2）：

```
[*] multicast route status=0 clean=1/1 step=0 errno=0 attempts=16 calls=1 success=1   # x4
[+] child is root!
[+] KernelSU ready
```

事后：`kernelsu ... (OE)`；`su -c id` → `uid=0(root) context=u:r:ksu:s0`。

Run 1b 失败尾部（W1 页选取耗尽，非 panic）：

```
[-] page ... stores an even byte over selinux_state.initialized; taking another
[-] prepare_kernel_page retry 4/4 +7006ms
[-]   heap spray failed
[-] W1: SELinux attempt 1 route failed; backing off
[-] Write 1 failed
```

## 判定

失败属 `KERNEL-PANIC-01` 同类环境/时序 flake（同构建冷机复跑 PASS、无 panic/无重启）；通过样本证据齐全。
日志：`T3c-20261003-multicast-direct-pass.native.log`（run 2）、`T3c-20261003-multicast-run1b-fail.native.log`（run 1b）。

## 变更说明

`recommend_shizuku` 项由 `backend.steps`（backend 私有 section，字符串 token → wire）取代；HOCON 二元项改 `true`/`false`；
native `safe_mode`/`vr_guard` 改 `bool`、`compact_waiter` 改 `optional<bool>`（布局未动，`session_layout_test` base=104/state=1560）。
`cmp_disasm --reviewed`：`do_one_write` OPERAND-SHIFT；`consumer_thread` 单条 `cbz w8`→`tbz w8,#0` 作为带理由 `REVIEWED_SHAPE` 登记。
