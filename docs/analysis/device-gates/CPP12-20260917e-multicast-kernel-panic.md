# CPP12e 真机门禁：Multicast（PI-TIMEOUT-01 v1 第三次复跑）— kernel panic（第 3 次）

- 构建：APK 228；native `6bd2291acca483861664d0d945c098e45e7dfadf26a9680fa5b75d110b6c08c6`（v1 恢复版；与 CPP12b/c 同一二进制）
- 入口：Direct；`enforce=unreadable`（W1 路径）
- bootreason：`kernel_panic,null`
- 日志：`CPP12-20260917e-multicast-kernel-panic.native.log`（143 行，panic 时截断）
- 结论：FAIL（第 3 次）

## 本次 panic 位置（与前两次不同）

- 已完成 **3 条 route**（W1、W1b、W2，均 `status=0 clean=1/1`），`Write 1 complete`（W1 阶段成功）。
- 最后一行：`[T+24752ms] PI route done`（W2 route 完成），此后截断。
- 无 `timed out` → PI-TIMEOUT 超时分支未触发。

## 三次 v1 panic 位置汇总（位置不一致）

| 运行 | 位置 |
|---|---|
| CPP12b | 首条 W1 route 前（spray 后） |
| CPP12c | 首条 W1 route 前（spray 后） |
| CPP12e（本次） | W2 route 完成后（攻击进行中） |

## 对照

| 二进制 | 运行 | 结果 |
|---|---|---|
| `6bd2291a…`（v1） | CPP12b/c/e | **3/3 panic**（位置不同） |
| `a5b2d151…`（基线） | CPP12d | 1/1 PASS（6/6 route） |

## 归因

- 三次 panic 位置**不一致**（两次 route 前、一次 W2 后）→ 指向**攻击本身的间歇性内核崩溃**（堆布局/竞态随机性），而非单一代码路径。
- 构建布局差异可能影响该随机崩溃的概率：v1 构建 3/3 失败、基线构建 1/1 通过（样本小，不能定论）。
- PI-TIMEOUT 逻辑与该崩溃无因果路径（超时分支未触发；攻击关键函数已由反汇编证明逐指令一致，见 `pi-timeout-binary-diff.md`）。

## 处置

- **回退 PI-TIMEOUT-01**，恢复到已验证基线 `a5b2d151…`；登记为“代码已实现并指令级验证，待后续批次在受控条件下重试”。
- 该间歇性内核崩溃作为**攻击层风险**单独登记（历史：CPP06c、CPP07 也有同模式记录，均发生在不同构建上）。
