# CPP13a 真机门禁：Multicast（resident 类化，步骤 A）— PASS（第一次冷机）

- 构建：APK 294；native `ea87d65f47eed56858d661febdd67da2734c6b50ec3c92ad5c844d810502c14f`
  （`fa9c5dd`：`MulticastWaiterRoute` 拥有 resident 生命周期，`route_operations.cpp` 留 thin wrapper）
- 入口：Direct（干净冷启动；`boot_ms=83768`）
- 设备：A301SO / 5.15.189-android13-8-00016-g51bba4309aac-ab14546557；CPU main=0 consumer=1
- 日志：`CPP13a-20260918-multicast-pass.native.log`（206 行）、`.ksu.log`（16 行）
- 结论：**PASS（第一次冷机）**；`do_one_write` 的 224→186 resident 分支形态变化未影响攻击链

## 覆盖点

- 6/6 route `status=0 clean=1/1 step=0 errno=0 calls=1 success=1`（`route_done` 6/6、`threads joined` 6/6）。
- `prepare_kernel_page ok attempt=1` ×5；**W1 连续两次页验收拒绝后第三次成功**（`retry 2/4`，总 ~4.4s）
  ——与 `CPP12s` 同型的环境窗口此次未触发 panic，再次印证 `KERNEL-PANIC-01` 的概率性。
- W1 / W1b / W2 / W2b / W3 TIF_SECCOMP / W3 seccomp mode 全链；
  `Write 1 complete` T+13258ms、`exploit complete` T+32890ms。
- `enforce=1 (enforcing)`、`KernelSU ready`。
- 形状对比 vs `e065ca70`：6/8 攻击函数 strict 一致、`waiter_thread` 1 处注解差异、
  `do_one_write` 224→186（差异块后非 resident 路径逐指令一致）。

## 判定

- resident 类化（所有权收归、wrapper 接口、host-safe init）行为等价；
- 按设计文档"至少多次冷机"要求，第二次冷机待跑或由用户豁免。

## 后续

- CPP13 步骤 B（one-shot 生命周期命名）与步骤 C（编码路径同源）收尾，随后勾选阶段。
