# 设备门禁：JNI 边界（J1/J2 + J3a/J4a 的拒绝面）· 2026-10-14

## 环境（adb 实测）

| 项 | 值 |
|---|---|
| 设备 | A301SO（transport_id 1，USB） |
| 内核 | 5.15.189-android13-8-00016-g51bba4309aac-ab14546557 |
| Android | 15；ABI arm64-v8a |
| SELinux | Enforcing |
| KernelSU | /data/adb/ksu 存在（adb shell 无 su；未加载/不可达） |
| 包 | com.ghostlock.app（debug，v1.3(914)-arm64-v8a） |

## 命令（可复现）

```sh
./gradlew :prepareGhostlockJniLibs :app:assembleDebug
adb install -r build/app/outputs/apk/debug/GhostLock-v1.3\(914\)-arm64-v8a-debug.apk
adb logcat -c && adb shell am start -n com.ghostlock.app/.tempui.TempUiActivity
adb logcat -d -s GhostlockGate:I
```

## 判据（真机原始日志，逐字）

```text
10-14 20:02:12.986 I GhostlockGate: gate: begin (boundary only; no exploit step is attempted)
10-14 20:02:12.989 I GhostlockGate: gate: cve_2026_43499 open ok handle=positive
10-14 20:02:12.989 I GhostlockGate: gate: cve_2026_43499 phase=INITIALIZING cleanup=CLEAN
10-14 20:02:12.992 I GhostlockGate: gate: cve_2026_43499 snapshot -> op=snapshot status=ok | phase=0 backend=43499 flags=0x801 width=8
10-14 20:02:12.993 I GhostlockGate: gate: cve_2026_43499 write_word -> op=write_word status=unsupported refusal=op-not-wired:write_word
10-14 20:02:12.994 I GhostlockGate: gate: cve_2026_43499 initialize -> op=initialize status=call-failed refusal=bad-parameters
10-14 20:02:12.994 I GhostlockGate: gate: cve_2026_43499 closed
10-14 20:02:12.994 I GhostlockGate: gate: cve_2026_43284 open ok handle=positive
10-14 20:02:12.994 I GhostlockGate: gate: cve_2026_43284 phase=INITIALIZING cleanup=CLEAN
10-14 20:02:12.995 I GhostlockGate: gate: cve_2026_43284 snapshot -> op=snapshot status=ok | phase=0 backend=43284 flags=0x1004 width=16
10-14 20:02:12.996 I GhostlockGate: gate: cve_2026_43284 write_block -> op=write_block status=unsupported refusal=op-not-wired:write_block
10-14 20:02:12.996 I GhostlockGate: gate: cve_2026_43284 initialize -> op=initialize status=call-failed refusal=bad-parameters
10-14 20:02:12.996 I GhostlockGate: gate: cve_2026_43284 closed
10-14 20:02:12.996 I GhostlockGate: gate: end
```

## 这几行各自证明了什么

| 行 | 证明 | 对应的判据 |
|---|---|---|
| `open ok handle=positive` | 库在真机上加载、GLKJ 建帧被 native 接受、句柄=（索引,代）发得出来 | J1 §3.1 建会话 |
| `snapshot ... phase=0 backend=43499 flags=0x801 width=8` | Snapshot 真实现：phase 由会话状态机发布；`0x801`=PI 窗口能力位(0x1)+宽度位(8<<8) | J2 状态机 / J3a 描述符 |
| `write_word ... unsupported refusal=op-not-wired:write_word` | 设备 seam 未接线时，写 op **具名**拒绝（不是沉默、不是成功） | J3a §3.3 规则 1/5 |
| `write_block ... refusal=op-not-wired:write_block` | 同上，且两个 backend 各自路由到自己的拒绝名 | J4a |
| `initialize ... call-failed refusal=bad-parameters` | 真后端以**空参数集**被调用时按名拒绝（参数面 J3b/J4b 才接） | J2 的 Initialize 诚实标注 |
| `closed` ×2 | shutdown+destroy 走通且幂等；关掉后会话不再被使用 | J2 生命周期 |

## 边界（这份证据**不**证明什么）

- 不证明任何写原语：`write_word`/`write_block` 在真机上的答案是 `op-not-wired`，因为设备 seam（J3b/J4b）尚未实现。
- 不证明 root、不证明 KernelSU 可加载、不证明 LKM：本门禁只走边界与拒绝面。
- 不证明 43284 的 SA/门/sequence：那条链需要会话帧与设备侧 splice 栈（J4b）。