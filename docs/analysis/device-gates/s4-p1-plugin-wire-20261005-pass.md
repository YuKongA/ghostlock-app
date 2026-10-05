# S4 · P1 插件 wire 层真机门禁 — **PASS（带边界说明）**（2026-10-05）

批次：`4d25d6e7`（native 第二步：`plugin.*` schema + `validate_plugin_wire` 绑定）+ `5983b684` / `5957a373`（Kotlin 发射与可编辑高级设置）。契约：`docs/analysis/contract-design.md` §3.14.7 + §3.14.7.7。

## 1. 方法

用手构 GLKv3 文档（Python 最小 MessagePack writer，键按 UTF-8 排序）+ **真插件**：把 aarch64 的 `glk_probe.so` 放到 `<GHOSTLOCK_HOME>/countermeasures/glk.probe/1.0/glk_probe.so`，再跑
`GHOSTLOCK_HOME=/data/local/tmp ./glk-p1b --ghostlock-app-call [--enable-status-record]`。

文档中的插件段（owner-qualified section `plugin.glk.probe`）：

```
enabled        = true                     # bool
stage          = "post_terminal"          # host stage token
module_path    = "glk.probe/1.0/glk_probe.so"   # 相对 <GHOSTLOCK_HOME>/countermeasures
module_hash    = "<sha256 of glk_probe.so>"     # 64 位小写 hex
params.threshold = 5                       # 动态键（联合类型 uint|int|bool|str）
```

## 2. 结果

| # | 场景 | 结果 |
|---|---|---|
| ① | **正确插件段** | 文档**通过校验与绑定**（打印 `profile_resolved kmi=5015 …`），43284 全链跑完：`lkm_window opened=1 closed=1 calls=0 abi_version=1 unload_ok=1`，**EXIT=0** |
| ② | `enabled=false` 出现在文档里 | **被拒**：`cannot load profile`（解析/校验层 fail-closed，对应 `DisabledPresent`） |
| ③ | `module_hash = "deadbeef"` | **被拒**：`cannot load profile`（对应 hash 形状校验） |

→ **wire 层语义在真机上成立**：插件段可被接受并**不干扰**既有攻击链；未启用/坏 hash 一律 fail-closed。

## 3. 边界说明（必须与「通过」一起读）

**本次未观察到插件的运行时调用**，且这不是测试方法问题——`grep` 检查：`src/core/pipeline/**` 对插件宿主（`plugin::{controller,host_ops,registry}`）**零引用**，唯一使用点是 `main.cpp` 的 `run_plugin_probe`（只读探针）。

即：**P1 完成的是「声明 → 校验 → 绑定」的 wire 层**；**「加载 → 按 stage 调用 → 卸载」的运行时接线尚未存在**（历史 `default_countermeasure_dir()` 也一直无生产接线）。

该接线属**攻击关键路径**（在生产进程内加载并执行第三方 `.so`）→ 需 **L 级设计**（加载/卸载窗口、阶段语义、失败是否致命、与 PI race 生命周期的关系）+ design 文档 + 真机门禁，已立为独立任务（不塞进 P1）。

## 4. 方法学备注（第二次同类错误）

`... | tail -3; echo EXIT=$?` 取到的是 **`tail` 的退出码**，不是被测程序的。本次两条负例的首轮结果因此一度显示 `EXIT=0`；改为不经管道捕获后确认为被拒。**门禁命令一律不得在被测命令与 `$?` 之间插管道。**
