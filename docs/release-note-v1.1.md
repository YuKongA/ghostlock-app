# GhostLock v1.1 Release Notes

构建：APK `343`（versionName `1.1`）· 攻击 native `0109d5a8` · 提取器 `libextract.so` 2.19 MB

## 亮点

- **新设备与 SoC 支持**：Google Tensor（`SOC_GOOGLE`，含物理加载回退）、Pixel 9 Pro / 9 Pro Fold、
  Honor Magic V5、NX809J/NX888J；内置 profile 增至 48 个
- **APK 瘦身**：远端 OTA 提取迁移到纯 Kotlin（HTTP range），Android 提取器不再携带 `http-rustls`
  栈（`libextract.so` **3.60 MB → 2.19 MB**）；同步启用 locale 过滤与 dex legacy packaging
- **Profile 编辑器（高级区）**：查看当前 profile 来源与覆盖状态、编辑执行参数（W1/W2/W3 尝试次数、
  W3 链轮数、路由等待、堆准备次数、select 延时/超时）、一键应用推荐核心、按 release 保存/清除覆盖
- **可靠性改进**：每次运行独立 KernelSU 日志路径（旧标记不再污染 handoff 判定）；W3 probe 失败
  改为退休 child 而非盲写；compact Select 路线内 4 次重试（每次重建 payload page）
- **上游行为对齐**：KernelSnitch range-end 截断、direct-map 末端测量、compact value/leaf 统一编码、
  arm-target 校验、W1 页面字节过滤、`SLIDE_*` direct-map task alias

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
