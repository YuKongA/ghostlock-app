# CPP12u 真机门禁：Multicast（批次 A：main 分层/分 TU）— PASS（第一次冷机）

- 构建：APK 288；native `53f9ec60d02c30fc1ac198a2255170af2bea7ab01208e53aef81af8181c3943e`
  （`085c060` main 解析/stage 拆分/victim enum/分 TU + `9a34332` 移除 stamp 短路）
- 入口：Direct（干净冷启动；`boot_ms=47760`）
- 设备：A301SO / 5.15.189-android13-8-00016-g51bba4309aac-ab14546557；CPU main=0 consumer=1
- 日志：`CPP12u-20260918-multicast-pass.native.log`（200 行）、`.ksu.log`（16 行）
- 结论：**PASS（第一次冷机）**；批次 A 的 main 拆分/分 TU 未引入行为回归

## 覆盖点

- 6/6 route `status=0 clean=1/1 step=0 errno=0 calls=1 success=1`（`route_done` 6/6、`threads joined` 6/6）。
- `prepare_kernel_page ok attempt=1` ×5 + 一次页验收重试（`retry 1/4`）后成功；无 error 级告警。
- W1 / W1b scratch repair / W2 / W2b prebuilt repair / W3 TIF_SECCOMP / W3 seccomp mode 全链；
  `Write 1 complete` T+13545ms、`exploit complete` T+33439ms。
- `enforce=1 (enforcing)`、`KernelSU ready`；KSU 日志 `late-load exit=0`、`KernelSU module loaded`。
- 基线告警仅 `SELinux enforce unreadable; assuming enforcing and running W1`。
- route 的 `step=0 errno=0` 为成功判定的强制清零（stamp 的 `setsockopt` 返回 `EADDRNOTAVAIL`
  是预期路径，成功条件为 `consumer_success > 0`；见 `CPP12s` 更正）。

## 判定

- 批次 A 相对回退版的行为等价性得到真机确认（首轮）；
- 按布局敏感规则需第二次冷机（reboot 回干净启动），或由用户豁免。

## 后续

- 批次 B（namespace 层：`d862a51`+`fb8fb59`+`dfd0d60`+`a30e8f2`）门禁通过后应用并各自门禁。
- 之后恢复 pid 所有权与 SELECT-01 的受控重试。
