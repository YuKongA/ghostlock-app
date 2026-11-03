### 用户裁定与单一 Flow 权威

Shizuku 不是运行前置条件。两个 Glued backend 都保留 App 进程启动器与 Shizuku 进程启动器；每次 run 由 app 在 Flow 入口检查 Shizuku 授权：已授权时走 Shizuku launcher，未授权/不可用时走 App launcher。用户原话：“shizuku并非必须，和app的进程启动器并行，如果有shizuku授权则通过shizuku启动进程，否则通过app权限”。

**执行器选择补充**：两个 Glued backend 的 Flow 始终由 App 持有；exec 阶段以 Shizuku binder 可达且权限已授予作为路由条件，满足则走 Shizuku process launcher，否则走 App process launcher。该选择不读取手动 `ExecutorPreference`；授权后 Shizuku 绑定、prepare 或 start 失败时保留 Shizuku 失败结果，不再启动 App 子进程；此规则不改变原版 backend 的执行语义。

Flow 与编排权威只在 App：UI → BackendRunDispatcher → FlowLaunchParent → 每 backend 的 SequentialFlow（各一个 exec step）→ ExecProcessLauncher。exec step 把同一份固定 ELF identity、argv/config、stdin/profile、finish/payload、run/log 交给选中的 process launcher。Shizuku UserService 只负责在 shell JVM 以固定 allowlist 启动/观测/回收指定 ELF；不解析 Flow symbol、不复制 SequentialFlow、不成为第二个编排权威。App launcher 在 App UID 下 ProcessBuilder 启动相同 ELF。原版 backend 仍禁用且不迁移。

| 路由条件 | 进程执行位置 | 失败语义 |
|---|---|---|
| Shizuku 已授权且 service 可用 | 共用 Shizuku UserService / shell JVM | 该 run 留在 Shizuku 路由；prepare/start/connection 不确定或失败时返回具名失败，不再自动启动 App 子进程，避免远端已启动但响应丢失时重复执行 |
| Shizuku 未授权或未运行 | App JVM / App UID | 用 App 权限直接 exec；本地启动/文件/API 权限不足时返回具名 NotReady/Failed，不伪造成功，也不尝试原版 JNI |

分支在任何 exec child 启动前确定；同一 run 恰好一个 launcher owner。Shizuku 与 App 仅共享 runner 输入契约和 terminal 结果形状，不共享子进程、fd、临时目录、IpSecRuntime 或执行状态对象。


Source SHA-256: ebb351dc94f24a58e6bbb40507e6faf16083265658ae2ee426623fd051053211
