# CPP13 设计：Multicast Waiter 路线类化（最后迁移）

> 目标：在 `native-cpp-migration-plan.md` 的 CPP13 边界内，把 Multicast 路线的 resident 状态与 one-shot 生命周期整理为显式 owner，同时不改变任何已验证的栈帧、系统调用顺序与 drain/close 语义。本文只做设计与基线记录，不含行为改动。

## 1. 现状边界

| 部分 | 位置 | 形态 |
|---|---|---|
| one-shot | `routes/route_operations.cpp::do_kernel5_fake_lock_route()` | 专用小栈帧 + 运行时 VLA stamp + `socket`/`setsockopt` + consumer 触发 + `close` |
| resident | `routes/multicast_waiter_route.cpp` 的 `multicast_resident_context` | `MulticastWaiterRouteContext`（futex、两个 worker、socket、调度策略、CPU、layout、`RouteStatus`） |
| 控制入口 | `kernel5_resident_start/write/stop()`；`waiter_thread` 经 `RouteController` dispatch | 全局 resident 单例 + 结构化 `RouteStatus` |
| payload 编码 | `payload_builder.cpp`（`encode_multicast_waiter` 等） | 纯函数，已 CPP05 化 |

## 2. 必须保持的敏感约束（来自 S13/S14/S15 与 CPP06/CPP07 门禁）

1. one-shot 的专用小栈帧、运行时 VLA、payload builder 调用、socket 与 `drain → success 读取 → destroy` 顺序；不要在敏感栈上加入 STL owner 或额外局部对象。
2. ghost disarm、consumer drain、success 读取、destroy 的顺序必须与 S14/S15 成功日志一致（`mcast ghost disarm ret=-1 errno=110` 等关键字不变）。
3. resident 与 one-shot 共用纯编码逻辑，但生命周期控制保持独立方法（`resident prepare/write` 与 `ghost disarm/destroy` 分离）。
4. `RouteStatus`/`RouteOutcome` 与日志文本、step/errno 数值映射不变。
5. 新增类型位于 `namespace ghostlock`，move-only，析构不得隐式关闭可能被内核引用的 fd。

## 3. 栈帧量化基线（native `625d5300…`，CPP12m/n 门禁版）

| 函数 | 序言 | 固定栈帧 |
|---|---|---|
| `do_kernel5_fake_lock_route` | `stp x29, x30, [sp, #-0x50]!` | 0x50（另有运行时 VLA `stamp[buffer_size]`） |
| `multicast_waiter_worker` | `sub sp, sp, #0xc0` + `stp x29, x30, [sp, #0x90]` | 0xc0 |
| `multicast_owner_worker` | `sub sp, sp, #0xa0` + `stp x29, x30, [sp, #0x80]` | 0xa0 |
| `waiter_thread` | `sub sp, sp, #0x1e0` + `stp x29, x30, [sp, #0x180]` | 0x1e0 |

迁移前后用 `llvm-objdump -d --no-show-raw-insn` 对比这些函数的序言与调用点；任何栈帧变化必须在提交说明中逐项批准。

## 4. 迁移步骤草案（每步独立提交 + 两次冷机门禁）

1. **步骤 A（resident owner 显式化）**：把 `multicast_resident_context` 从文件级单例改为 `MulticastWaiterRouteContext` 的一个命名实例（仍为进程级），`kernel5_resident_*` 通过访问器引用；worker 入口只经 `context` 访问状态（已是现状，补文档与注释）。
2. **步骤 B（one-shot 生命周期命名）**：把 `do_kernel5_fake_lock_route` 内的 socket/consumer 触发/close 片段提取为 `multicast_one_shot_prepare/execute/destroy` 静态函数或局部结构，但**不引入新的栈对象**（只做代码分组），保持 VLA 与调用顺序逐指令等价。
3. **步骤 C（共用编码路径）**：核对 one-shot 与 resident 的 stamp 编码同源（`encode_multicast_waiter`），删除重复字面量；用 `payload_builder_test` 固定向量锁定字节。
4. **步骤 D（收尾）**：把路线状态与 `disarm/destroy` 的显式顺序写入 `multicast_waiter_route.h` 注释，并在 `native-cpp-current-uml.md` 统一命名。

每步门禁：
- 主机：`make native-host-tests` + `multicast_waiter_route_test`；
- 二进制：`cmp` 脚本对比 8 个攻击关键函数（strict 或形状一致）+ 上表栈帧序言；
- 设备：两次冷机 Multicast 完整执行至 `KernelSU ready`；
- 失败即回退并记录（同 `PI-TIMEOUT-01`/`CPP12k` 流程）。

## 5. 风险与暂停条件

- `KERNEL-PANIC-01` 未解决前，任何布局敏感改动都可能被间歇性内核崩溃混淆；步骤 A/B 若在两次冷机内出现 panic，按先例回退并等待攻击层手段（dmesg/pstore 或多次统计方案）。
- one-shot 是唯一完整覆盖的敏感路径；不得为类化收益牺牲已验证的栈帧与顺序。
