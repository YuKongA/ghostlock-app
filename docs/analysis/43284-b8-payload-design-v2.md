# 43284 B8：PayloadScript 设计 v2

- 日期：2026-10-19
- 状态：待 design-critic 复评
- 取代：43284-b8-payload-design.md v1
- 上游：DFRoot@c274137 主链；payload-finish-chain-design v8

## 1. 关键修正

v1 的两个错误：

1. 用 root 写 `run-<ts>.log` 当完成信号；
2. 把 `payload done` 日志行当唯一判据。

v2 改为：

```text
App 写 run.sh（唯一文件写者）
root 只读 run.sh
root 只写 App 提供的 memfd 控制通道
App 读 memfd 的带 nonce 结构化帧
```

## 2. 控制通道

App 在攻击前创建两个 memfd：

```text
CTL_FD  控制帧
LOG_FD  日志
```

脚本拿到路径：

```text
/proc/<app_pid>/fd/<ctl_fd>
/proc/<app_pid>/fd/<log_fd>
```

控制帧格式固定：

```text
GLK1 <nonce> <stage> <code> <manager> <exit>\n
```

- nonce 由 App 生成，一次性；
- stage 是枚举；
- code 是数字；
- App 只接受匹配 nonce 的首帧；
- 超时/半帧/未知 nonce 都失败。

root 不写任何 app 文件；日志也只写 memfd。

## 3. PayloadScriptSpec

```kotlin
data class PayloadScriptSpec(
    val nonce: String,
    val ctlPath: String,
    val logPath: String,
    val commands: List<PayloadCommand>,
    val appPackage: String?,
    val lkm: LkmSpec?,
    val manager: ManagerSpec?,
    val bootstrap: BootstrapSpec?,
    val restoreSelinux: Boolean,
    val softReboot: Boolean,
)

sealed interface PayloadCommand {
    data class Shell(val argv: List<String>) : PayloadCommand
    data class App(val packageName: String) : PayloadCommand
    data class Insmod(val path: String, val params: List<String>) : PayloadCommand
}
```

不提供 `rawShell: String`。

Manager 最后执行：

```text
commands → app → lkm → bootstrap → manager
→ restoreSelinux → softReboot → ctl DONE
```

## 4. su 约束

Renderer 只生成结构化命令：

```text
Shell.argv 逐项白名单校验
禁止 argv[0] 为 su/which su
禁止 /system/bin/su、/system/xbin/su
禁止换行/NUL/超长
```

manager loader 只允许已安装 manager 的二进制路径。

脚本本身不需要 su；它由 LKM UMH 以 root 执行。

## 5. Manager

`ManagerSelector`：

```text
已安装包列表 → loader 路径 → 参数 → 版本
缺失 → MANAGER_MISSING
不可执行/失败 → MANAGER_FAILED
```

manager 缺失不阻断前面的 commands/app/lkm/bootstrap；
只有 manager 阶段返回非 OK。

`StageCode` 增加：

```text
MANAGER_MISSING
MANAGER_FAILED
BOOTSTRAP_FAILED
```

## 6. Bootstrap

BootstrapSpec：

```text
readPrefs
adoptZygoteEnv
setPartitionsRo
disableModules
runCtx
```

顺序在 manager 前；失败映射 BOOTSTRAP_FAILED；
KMI/路径从设备事实派生，不硬编码 android13-5.15。

## 7. defex

结论不变：**不迁移到脚本**。

- defex 是内核 `task_defex_enforce` 返回值；
- shell 不能改内核函数；
- 必须在自定义 LKM/敏感动作前生效；
- 可选方案是独立 `defex.ko`，仍是 LKM。

B9 把 `defex=1` 作为 LKM 显式配置，按设备/厂商启用；
修正 `LkmImagePatch` 固定 `defex=0` 的现状。

## 8. 文件/所有权

| 对象 | 创建 | 写者 | 读者 | 终结点 |
|---|---|---|---|---|
| run.sh | App | App | root | cleanup 删除 |
| CTL memfd | App | root | App | App close |
| LOG memfd | App | root | App | App close |
| LKM knob | App | App | LKM | 模块卸载 |

root 零文件写入；
App 是唯一文件写者。

## 9. 验收

```text
无 su 命令 token
manager 缺失仍完成 commands/app/lkm/bootstrap
manager 最后
CTL 帧 nonce 匹配
半帧/伪造/超时失败
cleanup 删除 run.sh
defex 仍在 LKM，配置显式
```

B8：renderer/manager/bootstrap/ctl/fake 测试。
B9：defex/restore_enforce/soft reboot/LKM 兼容/最终门禁。

