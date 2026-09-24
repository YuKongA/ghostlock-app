# B4 切片1 真机门禁：root_child handoff 迁出（multicast_waiter，direct）— PASS

对应候选 `build/native/ghostlock`（Batch 4 D1=B 切片 1，提交 `daf666c`）SHA-256
`4ee24fbccf7f7e20f3c0aaa9bb3860512c2bd2fcbb8c6ac2ee8f4651ee587bec`。
（Batch 4 薄声明基线为 `fcbc2191…`；本切片未改 8 攻击函数。）

## 设备与入口

- 型号 A301SO；`uname -r` = `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`。
- 入口：direct；KernelSU 未加载冷机；route = multicast_waiter；CPU 对 `main=0 consumer=1`。

## 结果

- `child is root!`；`[T+22105ms] exploit complete`；`KernelSU ready`；`enforce=1 (enforcing)`；无 panic。

## 变更说明

- `handoff` 从 `ExploitProcedure` 迁至 `session/root_child_frontend.{hpp,cpp}`（`run_root_child_handoff`）；
  `ExploitProcedure::handoff` 为薄转发；行为、语句顺序、日志文本不变。
- `cmp_disasm` 对 Batch 3 基线 8 攻击函数 PASS（handoff 不在 8 函数内）。

## 日志

- `Download/GhostLock/20260924-094351/ghostlock-direct-2.log.txt`。
