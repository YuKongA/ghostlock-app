# GhostLock v1.2 Release Notes

构建：Release APK `349`（arm64-v8a，**2.58 MiB**）· 攻击 native `0109d5a8` · 提取器 `libextract.so` 2.09 MiB（APK 内 1.03 MiB）

> 对照：Debug APK 约 20 MiB；APK 内 `libghostlock.so` 333 KiB、`classes.dex` 1.15 MiB（均为压缩后）。

## 亮点

- **新设备与 SoC 支持**：Google Tensor（`SOC_GOOGLE`，含物理加载回退）、Pixel 9 Pro / 9 Pro Fold、
  Honor Magic V5、NX809J/NX888J；内置 profile 增至 48 个
- **APK 瘦身**：远端 OTA 提取迁移到纯 Kotlin（HTTP range），Android 提取器不再携带 `http-rustls`
  栈（`libextract.so` **3.60 MB → 2.09 MiB**）；同步启用 locale 过滤与 dex legacy packaging，Release APK 仅 **2.58 MiB**
- **Profile 编辑器（高级区）**：查看当前 profile 来源与覆盖状态、编辑执行参数（W1/W2/W3 尝试次数、
  W3 链轮数、路由等待、堆准备次数、select 延时/超时）、一键应用推荐核心、按 release 保存/清除覆盖
- **可靠性改进**：每次运行独立 KernelSU 日志路径（旧标记不再污染 handoff 判定）；W3 probe 失败
  改为退休 child 而非盲写；compact Select 路线内 4 次重试（每次重建 payload page）
- **上游行为对齐**：KernelSnitch range-end 截断、direct-map 末端测量、compact value/leaf 统一编码、
  arm-target 校验、W1 页面字节过滤、`SLIDE_*` direct-map task alias

## All code changed, No bytecode changed!

Native 攻击链从 C 完整重写为 C++20（RAII / 命名空间 / 分层），而攻击关键指令保持逐指令形状不变。以下为同工具链、同优化档下的静态对比：

**相对上游 `main`（C 单模块 + LTO）**

| 指标 | 数值 |
|---|---|
| 文件大小 | 93,760 B → 1,140,328 B（+1116%，含静态 libc++/libc++abi） |
| `.text` | 67,072 B → 213,678 B（+218.6%） |
| 共同函数形状完全相同 | 115/137（83.9%） |
| 总体指令形状变化率 | 3,487/6,486 = **53.8%** |
| 核心攻击（上游可比独立符号） | `waiter_thread` −28.4% / 形状 89.8%；`do_one_write` −37.8% / 82.9%；`consumer_thread` +3.3% / 52.5%；`owner_thread` +13.8% / 44.6%；`tcp_punch_thread` −2.8% / 50.7% |

**相对上一门禁构建（`e13ed9dd`）**

| 指标 | 数值 |
|---|---|
| 共同函数形状完全相同 | 459/462（99.4%） |
| 总体指令形状变化率 | 6/33,073 = **0.02%** |
| 核心 8 攻击函数 | 全部 **0.0%**（逐指令形状不变） |

> 口径：指令形状 = 归一化全部地址与符号注解后的逐指令对比（差异数/基线指令数）；
> 核心 8 攻击函数 = `owner_thread`/`waiter_thread`/`consumer_thread`/`run_main_route_threads`/
> `do_kernel5_fake_lock_route`/`do_one_write`/`multicast_owner_worker`/`multicast_waiter_worker`。
> 上游为 LTO 单模块，`do_kernel5`/`do_pselect`/`run_main_route_threads`/multicast workers 被内联，
> 故上游侧只列可比独立符号。

## 相对上游 `main` 的结构变化

- `src/kernels/**/offsets.h`（C 注册表，48 个）由 `app/src/main/assets/kernel_profiles/` JSON profile 取代
- Native 核心统一为 C++20（原 `main.c`/`fops.c`/`util.c`/`offsets_json.c` 迁移为 `.cpp` 并分层）
- URL OTA 解析：App 内由 Kotlin 提取器完成；本地文件仍由 Rust 提取器处理
- 上游 CI 的 standalone 提取器（桌面版分发）不在本分支范围

## 已知限制

- **TCP Zerocopy / Select Stack** 仅有主机固定测试；无对应设备，尚未完成真机门禁
- URL OTA 路径尚无真机记录（本地文件路径长期使用）
- `KERNEL-PANIC-01`：Multicast 攻击存在间歇性内核崩溃，跨多个构建、同构建可出现 PASS/panic/PASS，
  判定为环境/时序而非布局/代码因果；门禁需 KernelSU 未加载的干净启动

## 升级说明

- 覆盖安装即可，用户导入的 offsets / 执行参数覆盖保留
- Direct 与 Shizuku 两种入口行为不变；日志路径、退出码与启动协议保持兼容

> 数据来源与证据索引：`docs/analysis/device-gates/`（Multicast 门禁链 CPP00–CPP17）、
> `docs/analysis/native-cpp-migration-plan.md`、`docs/pr-note-very-not-stable-dev.md`
