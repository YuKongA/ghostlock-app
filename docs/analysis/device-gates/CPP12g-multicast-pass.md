# CPP12g 真机门禁：Multicast（VictimPipes 收编）— PASS

- 构建：APK 233；native `7b60739a4024d1e8fc3fe91e5e8efab0c1961eb556a103fac23d9b962b80ce85`
- 入口：Direct；`enforce=unreadable`（完整 W1→W3）
- 日志：`CPP12g-multicast-pass.native.log`（192 行）
- 结论：**PASS**

## 覆盖点（victim 协议）

- `child_pid=19111 child_task=0xffffff893d715a00`；W2 `cred attempt 1/15`、W3 `TIF_SECCOMP+mode`/`seccomp mode` 全部通过。
- `handoff: child=19111 alive=1 sent=1 errno=0`；6/6 route `OK clean=1/1`；`exploit complete` T+30800ms、`KernelSU ready`。
- `child_pipes` 的 6 个 `UniqueFd`、`parked_cmd_w` 所有权转移、verify 函数 `.get()` 读写全部正常，无 fd 泄漏迹象（handoff 后 app 正常退出）。
