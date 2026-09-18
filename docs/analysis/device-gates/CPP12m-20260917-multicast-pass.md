# CPP12m 真机门禁：Multicast（VictimContext 小步收编）— PASS

- 构建：APK 247；native `625d5300f7312a44de802a3f48ab3757ce6d626c192c2109ed27c7280d654252`
- 入口：Direct（untrusted_app uid=10510）；`enforce=unreadable`（完整 W1→W3）
- 设备：A301SO / 5.15.189-android13-8-00016-g51bba4309aac-ab14546557；CPU main=0 consumer=1
- 日志：`CPP12m-20260917-multicast-pass.native.log`（206 行）
- 结论：**PASS**（首次冷机运行）

## 覆盖点

- 6/6 route `status=0 clean=1/1 step=0 errno=0 calls=1 success=1`；W2 `cred attempt 1/15` 一次命中。
- 6 次 spray 全部 `prepare_kernel_page ok attempt=1`；`threads joined` ×6；`W1b` scratch repair、`W2b` prebuilt repair 正常。
- `child is root!`、`child seccomp filter bypassed`、`handoff: child=24409 alive=1 sent=1 errno=0`、`enforce=1 (enforcing)`、`KernelSU ready`。
- `exploit complete` T+32997ms；无 fallback、无 dirty、无 panic。

## 本次收编范围

- `struct child_pipes` → `ghostlock::VictimContext`（header-only，`session/`），六个 `UniqueFd` 成员与顺序不变；child pid 仍留在 `run_exploit`（rooted victim 靠 EOF 自行完成 handoff，pid 退休语义需要独立门禁）。
- 提交前反汇编对比：640/640 函数指令形状一致、8/8 攻击关键函数严格逐指令一致、仅 `run_exploit` 两处 `bl` 注解位移（与 `CPP12k` 的 266 函数位移相比，布局扰动基本消除）。
- 与 `CPP12j`（`cea1bedc`）同一行为基线；按新规则本条布局敏感改动仍需要第二次冷机复跑确认。

## 不可判定项

- 设备温度与两次运行间隔未记录（日志不可判定）；resident 路线未启用。
