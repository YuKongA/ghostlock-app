# YuKongA/main 上游 Catch-up 分析（2026-09-13）

## 基线结论

- 当前分支：`decoupling-2`，HEAD `c944c20`。
- `origin/decoupling-2`：本地领先 6、落后 0；这里没有待拉取提交。
- 实际上游：`remote/main`（`https://github.com/YuKongA/ghostlock-app.git`）。
- 相对 `remote/main`：本分支独有 67 个提交，上游独有 3 个提交。
- 上游独有提交：
  - `50d2b72`：6.1 compact pselect 修复及配套安全、日志和 UI 改动。
  - `dfb0e84`：Lenovo Legion Y700 Wuji，内核 `6.12.38-android16-5-g74ad46052215-ab14494108-4k`。
  - `9ee07a8`：REDMI K80，内核 `6.1.138-android14-11-g965475777129-mi`。

界面此前显示“behind 2”很可能只计入两个设备提交，或尚未刷新 `50d2b72`；fetch 后 Git 图上实际是 3 个上游功能提交。

## 为什么不能直接 cherry-pick/全量覆盖

上游修改的 19 个路径中，16 个也被本分支重构过。上游实现仍依赖分散全局变量、C `src/kernels/offsets.h` 注册表和硬编码时序，而本分支已经迁移到 `TargetProfile`、`WriteRequest`、`ResolvedAddresses`、`HeapContext` 及 Kotlin 读取的逐设备 JSON。直接 cherry-pick 会重新引入已删除架构，并可能覆盖 S01–S09 已通过的真机兼容状态。

因此 catch-up 必须按行为语义移植；两个设备的 `offsets.h` 要转换为完整 JSON，并登记到 `index.json` 和独立支持设备文档。最终只有在逐项核对无遗漏后才合并 `remote/main` ancestry，并在冲突处保留本分支的等价新架构实现。

## `50d2b72` 行为拆分与当前架构落点

| 上游行为 | 风险/作用 | 本分支落点 |
|---|---|---|
| KernelSnitch 扫描在 range 末端截断 | 防止最后一个 coarse/slab 扫描越界 | 优先作为独立低风险修复移植到 `kernelsnitch.h`，添加边界测试 |
| `boot_ms` 与阶段 `fsync` | panic/回滚后保留日志，并可与 dmesg 时间对齐 | 接入现有每次攻击独立日志；同步点不得改变攻击时序关键窗口 |
| compact payload 的 value/leaf 统一编码 | 修复 6.1 pselect value 写入形状 | 移植到 `payload_builder` 纯函数并固定 value/leaf 测试向量 |
| payload arm/target 一致性检查 | 防止错误 arm 对目标执行破坏性写入 | 在 `WriteRequest` + `PayloadWriteLayout` 构建完成后验证，返回明确状态 |
| W1 页面字节过滤 | 避免页面地址字节破坏 `selinux_state.initialized` | 作为 Heap 页面验收策略；次数与超时继续读取 profile，不采用上游硬编码 12 次 |
| compact pselect 多 delay/timeout/retry | 提高 6.1 pselect 成功率并正确处理失败 | 延后到 S12 Select route context；所有候选 delay、超时和次数进入 profile schema |
| pselect 失败时保留 in-flight fd | 防止仍被内核引用的对象被提前回收 | S12 用显式 dirty/leaked ownership 状态实现，禁止普通 fallback |
| child pipe 提升到 route fd window 以上 | 防止 `dup2` 覆盖验证管道 | 独立移植并测试 fd 下界；之后由 victim/context 持有 |
| W3 probe 失败后退休 child | 禁止猜测方向并盲写 task 相邻字段 | S14 stage controller/victim 生命周期中移植，失败必须进入下一轮新 child |
| TCP 尝试上限及可恢复错误日志 | 避免失败路线耗尽 app timeout | S11 使用 profile 参数和 `RouteStatus`；不采用硬编码 128 次，不用日志级别代替状态语义 |
| 每次运行独立 KSU 日志、来源标签和 enforcing 检查 | 防止读取旧结果并改善 handoff 判定 | 与现有独立 Native 日志机制合并；Kotlin 生成路径并显式传给 Native |
| 6.1 TCP/pselect UI 开关 | 允许 compact 内核选择路线 | 保留为 UI 后续 TODO；Native 配置必须仍为单次 immutable snapshot |
| sparse override 继承 `compact_waiter` | 避免用户 override 清空内置能力 | 适配当前 Kotlin profile 合并规则并增加导入回归测试 |

## 新设备转换要求

两个上游设备都只有旧式 C 符号表，不足以直接成为本分支的完整内置 profile。转换时必须：

1. 从对应 6.1/6.12 模板复制完整结构字段与 execution 默认值。
2. 仅转录上游已验证的 release、waiter shift 和 `off_*`；结构字段必须与同内核族 ABI 核验，不能只因版本号相近而静默假定。
3. 为 REDMI K80 保留 6.1 `compact_waiter=1`、`pselect_waiter_shift=1`；为 Y700 使用 6.12 非 compact、shift 0。
4. 更新 `kernel_profiles/index.json`、中英文 `SUPPORTED_DEVICES*.md`，不恢复顶层 README 表格或 `src/kernels/offsets.h`。
5. JSON/schema、生成索引、Gradle 构建通过后仍标记“待对应设备真机门禁”，没有日志不得扩大已验证范围。

## 推荐集成顺序

1. U01-A：KernelSnitch 边界与日志耐久性，低耦合，可独立构建验证。
2. U01-B：payload arm/value/leaf 安全语义及 W1 页面验收，依托已完成的 S07–S09 对象。
3. U01-C：两个新设备转为 JSON profile，文档登记但保持待真机状态。
4. S10 后继续原解耦顺序；TCP 改动并入 S11，Select 改动并入 S12，W3/child/handoff 并入 S14。
5. U01-Z：逐项与 `50d2b72` diff 复核、合并 `remote/main` ancestry、解决冲突、全量构建并暂停真机回归。

这样可避免为追平 Git 数字而一次性混入 PI race、TCP、Select、victim 和 UI 五个不同风险域。
