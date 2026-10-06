# S4 · P3 参考插件 + 探针五键 header 真机门禁 — **PASS**（2026-10-05）

> **制品已移出（2026-10-05，`1c70b3e4`）**：参考插件现为独立项目 `ghostlock-plugin-example`（本仓库只留 `tools/plugins/README.md` 指引）；新构建路径可复现，sha256 因**源码路径映射变化**为 `b7e4891d…`，设备已复核 **EXIT=0**。本记录其余原始数据不回改。

批次：`940c404f`（P3 参考插件 + 构建/探针工具 + Rust 兼容五键）；被测量 `build/native/ghostlock`（含 `stage_availability`）。

## 1. 被测制品

| 项 | 值 |
|---|---|
| 源码 | `tools/plugins/glk_probe_plugin.c`：v2 描述符（`target_va` uint required、`label` str optional、`extract platform.abi.offset.init_task` uint required；`stage_mask = 1<<POST_TERMINAL`；caps `kernel_read,kernel_write`） |
| 构建 | `./tools/plugins/build.sh android`（NDK `30.0.16248370`，`aarch64-linux-android30-clang -std=c99 -O2 -Wall -Wextra -Werror -fPIC -shared -I src/core`） |
| 产物 | ELF 64-bit LSB shared object, **ARM aarch64**，唯一导出 `glk_entry` |
| sha256 | `5141c801fb95e0e62235b32b02c24e974123c6ef3a7f66d0465dec9022297736`（**与宿主机 `shasum -a 256` 一致**） |

## 2. 真机证据（A301SO）

```
$ GHOSTLOCK_HOME=/data/local/tmp ./glk-p3 --plugin-probe /data/local/tmp/ref_plugin.so   # EXIT=0
host_abi	1
countermeasures_root	countermeasures
host_stages	pre_spawn,post_spawn,pre_terminal,post_terminal
host_caps	kernel_read,kernel_write,alias,child_task
stage_availability	43499:pre_terminal;43284:post_terminal
plugin	glk.probe	1.0	1	80	5141c801…7736	post_terminal	kernel_read,kernel_write
hook	glk.probe	on_stage	post_terminal	0	glk.probe
param	glk.probe	target_va	uint	1	18446743524671239168	kernel VA the probe reads and writes back
param	glk.probe	label	str	0	-	diagnostic label for the glk.probe log line
extract	glk.probe	platform.abi.offset.init_task	uint	1	0	init_task image offset (base-relative), from the extractor profile path
```

- `size=80` 证明宿主走了 **v2 尾部读取**（旧 size 模块不读尾部）；`build.sh host` 的 `--expect-sha256` pin 自检 OK；
- sha 与宿主机实现一致（R8 合一后的 `support::sha256`）。

## 3. 探针 golden 刷新（4 键 → 5 键）

`app/src/test/resources/plugin-probe-golden.tsv` 现为 **12 行**（header 4+1 + plugin + hook + 4 param + 1 extract），内容 = 设备上 `glk_schema.so` fixture 的 stdout **逐字节**。
生成：`src/core/tests/fixtures/cm_test_plugin_schema.c` 交叉编译 → `/data/local/tmp/glk_schema.so` → `GHOSTLOCK_HOME=/data/local/tmp ./glk-p3 --plugin-probe …` → EXIT 0。消费方：Rust `plugin.rs`（T1）与 Kotlin `PluginProbeGoldenTest`。

## 4. 边界

- 本门禁**只覆盖探针与制品形状**：插件**运行时仍不被调用**（`src/core/pipeline/**` 对插件宿主零引用）= step 3 待接线；
- `stage_mask = POST_TERMINAL`：`/dev/glk` 只在 LKM 驻留窗口存在，而该窗口只在 43284 的 `post_terminal` 进入（contract-design §3.14.7.8）→ 参考插件按**可用阶段**声明；面向 43499 的插件应声明 `pre_terminal`。
