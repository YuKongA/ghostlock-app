# CPP12v 真机门禁：Multicast（批次 B：namespace 层）— PASS（第一次冷机）

- 构建：APK 290；native `1e196eb90a79ab82aa69d6fa2e3b76462ddc2ed2ec31ce62c97ad4119e4a23f6`
  （**与历史 `CPP12r` 2/2 panic 的构建逐字节一致**；含 `91fb8a1` namespace 层，stamp 短路已在 `9a34332` 移除）
- 入口：Direct（干净冷启动；`boot_ms=59426`）
- 设备：A301SO / 5.15.189-android13-8-00016-g51bba4309aac-ab14546557；CPU main=0 consumer=1
- 日志：`CPP12v-20260918-multicast-pass.native.log`（192 行）、`.ksu.log`（16 行）
- 结论：**PASS（第一次冷机）**；`main` 分层 + 分 TU + namespace 系列闭环，
  历史 2/2 panic 与布局无因果得到最终确认

## 覆盖点

- 6/6 route `status=0 clean=1/1 step=0 errno=0 calls=1 success=1`（`route_done` 6/6、`threads joined` 6/6）。
- **6/6 `prepare_kernel_page ok attempt=1`（零页重试）**；无 error 级告警。
- W1 / W1b scratch repair / W2 / W2b prebuilt repair / W3 TIF_SECCOMP / W3 seccomp mode 全链；
  `Write 1 complete` T+11204ms、`exploit complete` T+31816ms。
- `enforce=1 (enforcing)`、`KernelSU ready`；KSU 日志 `late-load exit=0`、`KernelSU module loaded`。
- 基线告警仅 `SELinux enforce unreadable; assuming enforcing and running W1`。

## 判定与历史闭环

- `1e196eb9` 曾在 APK 283 于 W1 spray 窗口 2/2 panic（`CPP12r`），当时整体回退；
- `CPP12s` 取得同一路径的内核栈（`rt_mutex_adjust_prio_chain` 读 ghost waiter 的 NULL `lock`，
  由本进程 `consumer_thread` 的 `sched_setattr` 触发），证明 panic 与布局无因果；
- 批次 A（`53f9ec60`，`CPP12u`）与批次 B（`1e196eb9`，本页）先后在干净冷启动 PASS，
  且批次 A↔B 的攻击关键函数指令形状一致（仅符号名/地址注解差异）。
- 按布局敏感规则，批次 A/B 各需第二次冷机，或由用户豁免。

## 后续

- `main` 分层/namespace 系列可从轮询状态改为完成；后续推进 CPP12 剩余项
  （pid 所有权、SELECT-01、`route_operations.cpp` 按路线拆分）与 CPP13/CPP14。
