# CPP12i 真机门禁：Multicast（KernelSuHandoff 探针结构化）— PASS

- 构建：APK 238；native `a7e1ea23e6393cfeb99d67e4ea596c972a41a2de6f5b5672cb4f2571f568c090`
- 入口：Direct（untrusted_app uid=10510）；`enforce=unreadable`（完整 W1→W3）
- 设备：A301SO / 5.15.189-android13-8-00016-g51bba4309aac-ab14546557；CPU main=0 consumer=1
- 日志：`CPP12i-20260917-multicast-pass.native.log`（230 行）
- 结论：**PASS**；handoff 探针（`HandoffProbeResult`/`handoff_probe_run`）在真机路径工作正常

## 覆盖点

- 8/8 route `status=0 clean=1/1 step=0 errno=0 calls=1 success=1`；8 次 spray 全部 `prepare_kernel_page ok attempt=1`；`threads joined` ×8。
- W2 出现一次攻击层随机未命中：attempt 1/15 的 route 报告成功但 `child uid = 10510`，attempt 2/15（含 W2b prebuilt repair）后 `child is root!` —— 与历史竞态随机性一致，不是状态误判。
- W1b `private scratch repaired; releasing quarantine`；W3 两次 route 后 `child seccomp filter bypassed (finit_module errno=22)`。
- 交接：`handoff: child=19899 alive=1 sent=1 errno=0`、`enforce=1 (enforcing)`、`KernelSU ready`；`exploit complete` T+36550ms（8 次 route）。
- 无 fallback、无 dirty failure、无 panic。

## 本次收编范围

- `/proc/modules` 扫描、root 日志标记扫描与 enforce 轮询收进 `ghostlock::handoff_probe_run`（显式 `HandoffPollPolicy` + `HandoffProbeResult`），日志文本、轮询次数/间隔与 `ready()`/enforcing 语义不变（提交 `eeb9d20`）。
- 主机固定测试 `handoff_probe_test` 锁定标记匹配、零策略短路与缺失日志目录。
- 提交前反汇编对比：8 个攻击关键函数中 7 个严格逐指令一致，`do_one_write` 仅 1 个地址注解位移；`run_exploit` 因内联轮询循环移出而重排。

## 不可判定项

- 设备温度未记录（日志不可判定）；resident 路线未启用。
