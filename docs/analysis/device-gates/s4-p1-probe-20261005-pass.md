# S4 · P1 插件探针真机门禁 — **PASS**（2026-10-05）

批次：`d951dd38`（P1-native 第一步：探针 + append-only ABI）；契约：`docs/analysis/contract-design.md` §3.14.7（含列序 `fec0f699`、勘误 `a52b852c`）。

## 1. 被测对象

`./glk-p1 --plugin-probe <path.so> [--expect-sha256 <hex>]`；被测插件 = `tools/plugins/build/glk_probe.so`（**aarch64**，`plugin glk.probe` v1.0，ABI 1，40 字节）。

## 2. 证据（设备 A301SO，Android 13 / 5.15.189）

| # | 场景 | 命令要点 | 结果 |
|---|---|---|---|
| 1 | 正常描述 | 无 flag | **EXIT 0**；header 4 行 + `plugin` 行 8 列 + `hook` 行 6 列，列序与 §3.14.7.2 表逐列一致 |
| 2 | `GHOSTLOCK_HOME` 变体 | `GHOSTLOCK_HOME=/data/local/tmp` | `countermeasures_root	countermeasures`（**相对名**，与冻结契约一致） |
| 3 | 无 home | 不设 `GHOSTLOCK_HOME` | `countermeasures_root	-`（= 不可校验，契约规定行为） |
| 4 | **哈希不匹配** | `--expect-sha256 000…0` | **EXIT 1** + `reject	-	HashMismatch`；host 测试另证 `open_lib` 计数 **0** → **未 dlopen** |
| 5 | 哈希匹配 | `--expect-sha256 180f24a5…180e` | **EXIT 0** + 完整描述 |
| 6 | 路径不存在 | `/data/local/tmp/nope.so` | **EXIT 1** + `reject	-	HashRejected` |
| 7 | 非库文件 | `/data/local/tmp/r4-default.bin` | **EXIT 1** + `reject	-	OpenFailed`（先哈希、后 dlopen 的顺序可见） |
| 8 | 入口互斥 | `--plugin-probe … --ghostlock-app-call` | **EXIT 255** + `choose one entrypoint` |

**交叉实现一致性**：设备上探针自算的 `sha256` = `180f24a55fd9ee8d66991e0ce0d04d2c36bc8b7dcc9a3e84d5b99b7f3b49180e`，与**宿主机** `shasum -a 256` 对同一 `.so` 的值**逐字符相同**（R8 合一后的 `support::sha256` 在 bionic/aarch64 上与标准实现一致）。

```
host_abi	1
countermeasures_root	countermeasures          # 或 - （无 GHOSTLOCK_HOME）
host_stages	pre_spawn,post_spawn,pre_terminal,post_terminal
host_caps	kernel_read,kernel_write,alias,child_task
plugin	glk.probe	1.0	1	40	180f24a5…180e	post_terminal	-
hook	glk.probe	on_stage	post_terminal	0	glk.probe
```

## 3. 方法学备注（避免误读）

第一次跑「路径不存在 / 非库文件」时把 stdout 管道给了 `tail`，`echo EXIT=$?` 取到的是 **`tail` 的退出码（0）**，并非探针的。改为不经管道后确认为 **EXIT 1**；上表为修正后的结果。

## 4. 结论与边界

- 探针在**真机 bionic/aarch64** 上行为与冻结契约一致：成功 0 + TSV、失败非 0 + `reject` 行（原因复用 `LoadStatus` 名）、入口互斥、**哈希先于 dlopen**；
- **未覆盖**（待 P1 第二半）：`plugin.*` wire 字段的绑定与文档发射、App 内导入闭环、`delta4` 试验台端到端——见 `task-8` / `task-4`；
- 本批未触碰攻击关键路径（探针为独立只读入口）；无 wire 变化。
## 5. 附：带 param/extract 的完整 stdout（作 Kotlin 对拍 golden）

fixture 源：`src/core/tests/fixtures/cm_test_plugin_schema.c`（`src/Makefile` 里 host 侧也用它）。

交叉编译（NDK `30.0.16248370`）：

```
aarch64-linux-android30-clang -shared -fPIC -I src/core \
  -o /tmp/glk_schema_arm64.so src/core/tests/fixtures/cm_test_plugin_schema.c
```

设备：模块推送为 `/data/local/tmp/glk_schema.so`；被测 native 二进制为 `/data/local/tmp/glk-p1b`（由 `build/native/ghostlock` 推送）。

命令与结果：

```
cd /data/local/tmp && GHOSTLOCK_HOME=/data/local/tmp ./glk-p1b --plugin-probe /data/local/tmp/glk_schema.so
```

→ **EXIT 0**，stdout（制表符分隔，逐字节落入 `app/src/test/resources/plugin-probe-golden.tsv`）：

```
host_abi	1
countermeasures_root	countermeasures
host_stages	pre_spawn,post_spawn,pre_terminal,post_terminal
host_caps	kernel_read,kernel_write,alias,child_task
plugin	test.schema	1.2.3	1	80	decc767346129b6dea4a8fb8d907daa13c48d9a44fd60645d5e57e42614205cf	pre_spawn,post_terminal	kernel_read,alias
hook	test.schema	on_stage	post_terminal	10	schema-hook
param	test.schema	threshold	uint	1	200	uint parameter
param	test.schema	mode	str	0	auto	string parameter
param	test.schema	enabled	bool	0	1	bool parameter
param	test.schema	delta	int	0	0	-
extract	test.schema	task_offset	uint	1	0	extractor-provided offset
```

该 golden 覆盖**动态键路径的全部四种类型**（uint/str/bool/int）、`required` 1/0、空 doc 写 `-`、多值 `stages`/`required_caps`（逗号分隔）与 `hook` 行——Kotlin 侧 `PluginProbeGoldenTest` 与 adapter 的联合类型解析应以此为硬断言。

落盘路径 `app/src/test/resources/plugin-probe-golden.tsv`（commit `7d54ce78`）；消费方 `app/src/test/kotlin/com/ghostlock/app/data/plugin/PluginProbeGoldenTest.kt`。

> **编辑注（2026-10-05）**：§5 曾因用 shell `printf '%s'` 追加 JSON 转义串而损坏（`\n` 未还原、反引号内容被 shell 吞掉），整段退化为一行；本次按上述权威源还原（提交标题 `docs(device-gates): restore the escaped §5 of the P1 probe gate record`），stdout 代码块与 golden 文件**逐字节一致**。
