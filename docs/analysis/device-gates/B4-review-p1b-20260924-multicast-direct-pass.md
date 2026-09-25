# B4 架构审查修正 真机门禁：middleware 模板绑定（multicast_waiter，direct）— PASS

对应候选 `build/native/ghostlock`（Batch 4 架构审查 P1-B/P2/P3/P4，提交 `32ed4f9`）SHA-256
`cacc2c6c7530710d6ec0ec7085452389fe58f6dfd0a7e286948fdf517e5040f9`。
（基线 `bd35b701…`（pipeline 落地候选），由提交 `1241ced` 重建复现。）

## 设备与入口

- 型号 A301SO；`uname -r` = `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`。
- 入口：direct；KernelSU 未加载冷机；route = multicast_waiter；固定 CPU 对。

## 结果

- PASS（用户确认，2026-09-24）。日志归档于设备 `Download/GhostLock/`。

## 变更说明

- backend 模板化接收 middleware policy（P1-B）：`Cve2026_43499Policy::run<M>` / `attack_write<M>`，
  `M::resident_write` / repair hook 直接静态调用；`MulticastPolicy` 的 4 个 hook 为
  `[[gnu::noinline]]` 边界；`route/middleware_hooks.*` 删除；三个 policy 显式实例化。
- `combination_supported()` 单一派发权威（P2）；`RunResult` 终态语义（P3）；`VictimChain` 契约（P4）。
- `cmp_disasm`（基线 `bd35b701…`）：`owner_thread` / `multicast_*_worker` IDENTICAL；
  `waiter_thread` / `consumer_thread` / `run_main_route_threads` / `do_kernel5_fake_lock_route`
  LAYOUT-SHIFT（仅数据地址注解平移）；`do_one_write`（Multicast 实例）1 处等价取址差异（已复核）。
