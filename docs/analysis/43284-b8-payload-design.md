# 43284 B8：PayloadScript 设计（无 su 依赖 + manager 最后）

- 日期：2026-10-19
- 状态：待 design-critic 评审
- 上游：DFRoot@c274137 主链；payload-finish-chain-design v2
- 取代：临时 `PayloadScript.kt`（files/payload + payload.status + /dev/dfm6）

## 1. 目标

1. payload 脚本由 GhostLock 生成，不依赖 `su` 可执行文件。
2. root manager 可选，默认最后加载；manager 不存在时，前面的 payload 仍然执行并记账。
3. 不写 root status/marker/signal；完成信号走 app 白名单日志。
4. 评估 defex 从 LKM 移到脚本的可行性。

## 2. PayloadScriptSpec

```kotlin
data class PayloadScriptSpec(
    val commands: List<String> = emptyList(),
    val appPackage: String? = null,
    val lkmPath: String? = null,
    val lkmParams: String = "",
    val manager: ManagerSpec? = null,
    val restoreSelinux: Boolean = false,
    val softReboot: Boolean = false,
    val logPath: String,
    val order: List<PayloadKind> = listOf(
        PayloadKind.Command,
        PayloadKind.App,
        PayloadKind.Lkm,
        PayloadKind.Manager,
    ),
)
```

`ManagerSpec` 由已安装 manager 选择：

```text
packageNames / loaderPath / lateLoadArgs
```

不调用 `su`；脚本本身由 LKM `call_usermodehelper(sh -c script)` 以 root 执行。

## 3. 文件与所有权

| 文件 | 写者 | 读者 | 说明 |
|---|---|---|---|
| `files/finish/run.sh` | App | LKM/root | 0700，原子写，cleanup 删除 |
| `files/finish/run-<ts>.log` | root 脚本 | App | 仅日志，不承载控制信号 |

LKM knob 只带 `run.sh` 绝对路径。

删除：

```text
files/payload
files/payload.status
/dev/dfm6
```

## 4. 执行顺序

```text
Command
  → App
  → Lkm
  → Manager（可选，最后）
  → restoreSelinux / softReboot（可选）
```

manager 不存在或 late-load 失败：

```text
log: manager-missing / manager-failed
payload core: 已执行部分仍然有效
StageCode: OK + manager warning，或 MANAGER_FAILED（按开关）
```

不因为 manager 失败回滚已完成的 payload。

## 5. 完成信号

脚本只写日志：

```sh
log 'payload start'
...
log 'payload done'
```

失败：

```sh
log 'payload failed <stage> <code>'
```

`WaitStage` 轮询 `run-<ts>.log`：

```text
payload done   → OK
payload failed → 具名 StageCode
timeout        → WAIT_TIMEOUT
```

不再使用 `/dev/df*` 作为完成信号；`/dev/df` 只是 libcxx mutex，不作为模块完成。

## 6. su 依赖

禁止：

```text
su -c
/system/bin/su
/system/xbin/su
which su
```

脚本以 UMH root 身份运行，直接执行：

```sh
mkdir /data/adb/...
insmod ...
am force-stop ...
monkey ...
```

manager loader 只在最后尝试：

```sh
if [ -x "$BIN" ]; then "$BIN" late-load ...; fi
```

## 7. defex 评估

结论：**不把 defex 移到 payload 脚本**。

原因：

1. defex 是内核态 `task_defex_enforce`，shell 不能改内核函数返回值。
2. defex 必须在加载自定义 LKM/执行敏感 payload **之前**生效；脚本运行时 LKM 已 init。
3. 当前 LKM 用 kprobe 把返回值改为 0；这是内核代码，不是脚本能替代的。
4. 若强行搬到 userspace，只能再加载一个 defex LKM；仍是 LKM，不是脚本，且增加第二次模块加载风险。

保留：

```text
GhostLock LKM defex=1（按设备/厂商启用）
```

可选 B9：拆出独立 `defex.ko`，由 payload 在其它动作前 insmod；不作为默认路径。

## 8. 与现有代码的迁移

| 当前 | 目标 |
|---|---|
| `PayloadScript.kt` 固定文本 | `PayloadScriptRenderer` + `PayloadScriptSpec` |
| `files/payload` | `files/finish/run.sh` |
| `payload.status` | 删除；日志 `run-<ts>.log` |
| `/dev/dfm6` | 删除；WaitStage 读日志 |
| 固定 `ksud` 第一段 | manager 最后，可选 |
| `LkmImagePatch` 固定 path | 注入 renderer 产出的 run.sh 路径 |

## 9. 验收

```text
无 su 字符串
manager 缺失时 command/app/lkm 仍执行
manager 最后执行
payload done / failed 日志可读
cleanup 删除 run.sh，保留日志
LKM cmd 指向 files/finish/run.sh
```

## 10. 批次

B8：PayloadScriptSpec/renderer、日志信号、manager 选择、无 su、测试。
B9：defex 独立模块评估、restore_enforce、soft reboot、LKM 兼容矩阵。

