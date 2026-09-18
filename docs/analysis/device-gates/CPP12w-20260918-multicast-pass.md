# CPP12w 真机门禁：Multicast（VictimContext pid 所有权）— PASS（第一次冷机）

- 构建：APK 292；native `e065ca70772cb2b707d8456145f5a0273c817648c8e0f640e8fa777ecabe296a`
  （`4a6cb5f`：VictimContext 拥有 child pid；历史 `b147df9f` 等效改动的重做版）
- 入口：Direct（干净冷启动；`boot_ms=50175`）
- 设备：A301SO / 5.15.189-android13-8-00016-g51bba4309aac-ab14546557；CPU main=0 consumer=1
- 日志：`CPP12w-20260918-multicast-pass.native.log`（201 行）、`.ksu.log`（16 行）
- 结论：**PASS（第一次冷机）**；pid 所有权收归未引入回归，`CPP12k` 的 2/2 归因再次被环境窗口解释

## 覆盖点

- 6/6 route `status=0 clean=1/1 step=0 errno=0 calls=1 success=1`（`route_done` 6/6、`threads joined` 6/6）。
- `prepare_kernel_page ok attempt=1` ×5 + 一次页验收重试（`retry 1/4`）后成功；无 error 级告警。
- W1 / W1b / W2 / W2b / W3 TIF_SECCOMP / W3 seccomp mode 全链；
  `Write 1 complete` T+15032ms、`exploit complete` T+34573ms。
- `enforce=1 (enforcing)`、`KernelSU ready`；KSU 日志 `KernelSU module loaded`。
- 形状对比 vs namespace 构建 `1e196eb9`：6/8 攻击关键函数严格一致，
  `waiter_thread`/`do_one_write` 仅 1 处地址注解差异；主机 `victim_context_test` 通过。

## 判定

- pid 所有权（set/release/mark/retire）在真机上行为等价：park/`kill(-pid)`/W3 丢失/handoff 全部走显式转移与退休语义；
- 历史 `CPP12k` 的 2/2 W2 spray panic 与 `CPP12l` 隔离 PASS 已证明该构建的环境间歇性，本次进一步确认；
- 按布局敏感规则需第二次冷机，或由用户豁免。

## 后续

- CPP13（Multicast 类化）为主要剩余迁移项；SELECT-01 与 `route_operations.cpp` 拆分受
  Select/TCP 设备限制，先做可验证部分。
