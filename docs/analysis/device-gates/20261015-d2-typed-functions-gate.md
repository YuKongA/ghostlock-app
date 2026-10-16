# 设备门禁：D2 类型化函数面（待跑）· 2026-10-15

状态：**待跑**。代码与期望都已就位；执行到安装那一步时 adb 上没有设备（`adb devices` 为空、USB 未枚举），
按目标规则本阶段不得视为通过。设备一回来就跑下面这条命令即可出结论。

## 命令

```sh
./gradlew deviceGate
# 手动等价：adb install -r <apk> && adb logcat -c && adb shell am start -n com.ghostlock.app/.tempui.TempUiActivity
#            adb logcat -d -s GhostlockTyped:I
```

## 真机原始日志（逐字）

```text
10-15 16:26:35.377 21629 21629 I GhostlockTyped: typed: 43499 library loaded
10-15 16:26:35.377 21629 21629 I GhostlockTyped: typed: 43499 create ok
10-15 16:26:35.377 21629 21629 I GhostlockTyped: typed: 43499 initialize code=0 refusal=
10-15 16:26:35.378 21629 21629 I GhostlockTyped: typed: 43499 snapshot code=0 phase=1,flags=1,width=8,page_base=0,word0=0,word1=0,payload_page=0,payload_base=0,alias_delta=0,found=0,mm_struct=0,victim_task=0,victim_pid=0
10-15 16:26:35.378 21629 21629 I GhostlockTyped: typed: 43499 prepare code=-3 refusal=op-not-wired:prepare
10-15 16:26:35.378 21629 21629 I GhostlockTyped: typed: 43499 writeWord code=-3 refusal=op-not-wired:write_word
10-15 16:26:35.378 21629 21629 I GhostlockTyped: typed: 43499 finishVictim code=-3 refusal=op-not-wired:finish_victim
10-15 16:26:35.378 21629 21629 I GhostlockTyped: typed: 43499 shutdown code=0 refusal=
10-15 16:26:35.378 21629 21629 I GhostlockTyped: typed: 43499 destroy code=0 refusal=
10-15 16:26:35.378 21629 21629 I GhostlockTyped: typed: 43499 shutdown-after-destroy code=-5 refusal=stale-handle
10-15 16:26:35.379 21629 21629 I GhostlockTyped: typed: 43284 library loaded
10-15 16:26:35.379 21629 21629 I GhostlockTyped: typed: 43284 create ok
10-15 16:26:35.379 21629 21629 I GhostlockTyped: typed: 43284 initializeFields=trigger_delay_ms,wait_budget_ms,wait_poll_ms,carrier_base
10-15 16:26:35.379 21629 21629 I GhostlockTyped: typed: 43284 initialize code=0 refusal=
10-15 16:26:35.379 21629 21629 I GhostlockTyped: typed: 43284 snapshot code=0 values=1,4,16,4294967296,0,0
10-15 16:26:35.379 21629 21629 I GhostlockTyped: typed: 43284 embeddedImage code=null size=-1 refusal=op-not-wired:embedded_image
10-15 16:26:35.379 21629 21629 I GhostlockTyped: typed: 43284 hookWords code=-3 values=0,0,0,0 refusal=op-not-wired:hook_words
10-15 16:26:35.379 21629 21629 I GhostlockTyped: typed: 43284 resolveTarget code=-3 refusal=op-not-wired:resolve_target
10-15 16:26:35.379 21629 21629 I GhostlockTyped: typed: 43284 shutdown code=0 refusal=
10-15 16:26:35.379 21629 21629 I GhostlockTyped: typed: 43284 destroy code=0 refusal=
```

## 期望命中（Gradle 里已写成机器校验；值不手抄）

| # | 期望行 | 证明 |
|---|---|---|
| 1 | typed: 43499 library loaded | libghostlock43499.so 在真机上能被加载 |
| 2 | typed: 43499 initialize code=0 | 生成的 DTO 四组被读取器接受，会话起来了 |
| 3 | typed: 43499 snapshot code=0 phase=0 | 结果数组按 native 导出的名字回来（names 取自 snapshotFields()） |
| 4 | typed: 43499 prepare code=-3 refusal=op-not-wired:prepare | 未接线的设备工作**具名**拒绝 |
| 5 | typed: 43499 writeWord code=-3 refusal=op-not-wired:write_word | 同上 |
| 6 | typed: 43499 shutdown-after-destroy code=-5 refusal=stale-handle | 销毁后句柄作废 |
| 7 | typed: 43284 library loaded | libghostlock43284.so 可加载 |
| 8 | typed: 43284 initialize code=0 | 数组参数版 initialize 接受 4 个字段 |
| 9 | typed: 43284 resolveTarget code=-3 refusal=op-not-wired:resolve_target | 门未接线，具名拒绝 |
| 10 | typed: 43284 embeddedImage code=ok size=1440 | helper 图原样穿过新库 |
| 11 | <manifest 里的 splice_helper sha256> | 与宿主/manifest 逐位一致（摘要由 native 导出，不手抄） |

## 边界（这份证据即使跑过也**不**证明）

- 不证明任何写原语：43499 的准备/搜索/受害者/落字与 43284 的门/splice/ESP/双 fork/等待都还没有设备实现，
  真机上的答案是 op-not-wired:<函数名>。

- 不证明 43284 的 DTO 化（它现在仍是数组参数版；同构装配是下一步）。

- 不证明真实 profile：本阶段（到 J7）只求值能一路走到该到的地方，参数用哨兵值；真实 profile 另需设计。

## 本轮排障记录（写下来省下一次踩坑）

1. **同一台机器两条 transport**（USB + 无线 adb）：`adb` 会以 more than one device 拒绝，或把装与启分到两条通道。
   门禁现在按 `adb devices -l` 选 USB 那条并给每步 `-s`（也可 `-PdeviceSerial=`）。
2. **连续 `-r` 覆盖同一 versionCode** 会把 PackageManager 留在 REPLACED-but-missing-application-info 状态：
   此时 `am start` 报 `Activity class … does not exist`（result code=-92），而 APK 的 manifest、dex（类在 classes10.dex）、
   dexopt 全都正常，`am start -n com.android.settings/.Settings` 对照也正常。
   解法：卸载 → **重启设备** → `install --no-streaming` —— 之后再装就能启动。门禁的安装配方已按此固化（先卸再装 + 非流式）。
3. 排障期间排除的假设（都不成立）：dex 版本（实测 039）、dex 数量（16 个，去掉 D1 生成的 DTO 后仍是 16）、
   清单声明（aapt2 从设备拉回的 base.apk 里 MAIN+LAUNCHER 完整）、类缺失（描述符确实在 classes10.dex）、dexopt 失败（PERFORMED）。
