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

---

## 第二次门禁：43284 的数据 op（J4b 的第一片）· 2026-10-14 20:13

同一条命令链（assembleDebug → adb install -r → am start → logcat -s GhostlockGate:I）。
这一批把 43284 的两个**不需要设备**的 seam 接上：`EmbeddedImage`（本构建自带的两张图）与
`HookWords`（sentry 计划的两个词与两个偏移）。其余 seam（门/splice/ESP/双 fork/终态等待）仍为 null ⇒
仍旧 `op-not-wired`。

### 判据：真机日志 vs 宿主/清单（同一份字节）

| 项 | 真机 logcat | 宿主测试 / manifest | 一致 |
|---|---|---|---|
| splice_helper size | 1440 | 1440 | ✓ |
| splice_helper sha256 | 4b4649cde20dcfb7a159300742d07da6658ade38034f8dcdb4ff4000ba2ea53c | 同值（`image splice_helper`，manifest 导出） | ✓ |
| landing_payload size | 480 | 480 | ✓ |
| landing_payload sha256 | 4bf82b9d55f3b3b04c03afe9e555bcd5e7322f4cef36d68824e09622cfcf388d | 同值（`image landing_payload`） | ✓ |
| branch word | 0x14012d83 | 0x14012d83（jni_ops_43284_device_test） | ✓ |
| displaced word | 0xf9400028 | 0xf9400028 | ✓ |
| site / landing | 733356 / 1041960 | plan.hook_file_offset / plan.payload_file_offset | ✓ |

```text
10-14 20:13:41.603 I GhostlockGate: gate: image splice_helper op=embedded_image status=ok size=1440 sha256=4b4649cde20dcfb7a159300742d07da6658ade38034f8dcdb4ff4000ba2ea53c
10-14 20:13:41.605 I GhostlockGate: gate: image landing_payload op=embedded_image status=ok size=480 sha256=4bf82b9d55f3b3b04c03afe9e555bcd5e7322f4cef36d68824e09622cfcf388d
10-14 20:13:41.607 I GhostlockGate: gate: hook_words op=hook_words status=ok branch=0x14012d83 displaced=0xf9400028 site=733356 landing=1041960 payload=480
```

### 这一片证明了什么 / 不证明什么

- 证明：图与计划**原样穿过 JNI**（字节级摘要两端一致），EmbeddedImage/HookWords 的具名语义、宽度与长度都在真机成立。
- 不证明：门、splice、ESP、双 fork、终态等待 —— 它们仍是 `op-not-wired`，属于 J4b 剩余部分。

### 本轮顺带修掉的两个真问题

1. `:nativeBuildAndroid` 没有声明 inputs ⇒ 源码改了任务仍判 UP-TO-DATE ⇒ APK 装的是旧 .so（第一次跑门禁时
   image 三行全静默暴露了它）。现已声明 `core/src/**` + `core/CMakeLists.txt`，并把 .so 也列为输出。
2. 门禁里 `EmbeddedImage`/`HookWords` 只打尺寸不打状态 ⇒ 拒绝/旧库看起来像"空数据"。现在每次回答都带
   `status=` 与（非 Ok 时）`refusal=`。

---

## 第三次门禁：换成自写日志系统之后（2026-10-14 20:21）

native 侧去掉了 spdlog（用户裁定），改成 support/log.hpp 里的 printf 实现；Kotlin 侧新增
GhostLog（级别 + logcat + 定长环 + 错误带栈）。门禁本身没有变，只是它的行现在经由这两套日志：

```text
10-14 20:21:56.194 I GhostlockGate: gate: image splice_helper op=embedded_image status=ok size=1440 sha256=4b4649cd…a53c
10-14 20:21:56.199 I GhostlockGate: gate: hook_words op=hook_words status=ok branch=0x14012d83 displaced=0xf9400028 site=733356 landing=1041960 payload=480
10-14 20:21:56.176 I GhostlockUi: tempui: 临时界面已启动
```

三行一起证明：native 的新日志不改变任何判据值；Kotlin 的日志按**调用方标签**落到 logcat（门禁查
GhostlockGate、界面查 GhostlockUi 各取各的），且 logcat 里不再出现 `I/<tag>: I/<tag>:` 这种双重前缀。
