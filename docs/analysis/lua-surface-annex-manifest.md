# 附件：surface manifest 原文（派生源 · 19 行，逐字节）

> 分析类（≤8192 B）。**来源** = `make -C core surface-manifest`（pin = `core/src/tests/surface_manifest_test.cpp`）。
> **用途**：供 `make -C core surface-doc-check DOC=docs/analysis/lua-surface-annex-manifest.md` **逐行对拍**（必须 EXIT=0）；设计主件只留指针与验收说明。
> **列（按导出器源码顺序）** = 条目 · 形态 · 实现层 · **phase**（词表：`stage` · `pre_window` · `window` · `terminal`）· **持资源(`holds_resource`，col5)** · 可记录 · 可执行 · 参数名 · 必填 · 上界。
> **⚠ 列序以导出器源码为准**（`core/src/tests/surface_manifest_test.cpp` 头注 `name·form·implements_layer·phase·holds_resource·recordable·executable·arg_key·arg_required·arg_ceiling`）；**引用时必须核对列号**（本次一次误读即因列序写反）。

```tsv
backend	call	backend	pre_window	true	true	false	-	-	-
writeop	handle_method	backend	window	false	true	true	handle	true	18446744073709551615
writeop	handle_method	backend	window	false	true	true	target_kind	true	64
writeop	handle_method	backend	window	false	true	true	target_value	true	18446744073709551615
writeop	handle_method	backend	window	false	true	true	value	true	18446744073709551615
writeop	handle_method	backend	window	false	true	true	width	true	8
copyop	handle_method	backend	window	false	true	true	handle	true	18446744073709551615
copyop	handle_method	backend	window	false	true	true	src_kind	true	64
copyop	handle_method	backend	window	false	true	true	src_value	true	18446744073709551615
copyop	handle_method	backend	window	false	true	true	dst_kind	true	64
copyop	handle_method	backend	window	false	true	true	dst_value	true	18446744073709551615
copyop	handle_method	backend	window	false	true	true	width	true	8
settle	handle_method	backend	window	false	true	true	handle	true	18446744073709551615
settle	handle_method	backend	window	false	true	true	us	true	1000000
handoff	handle_method	terminal	terminal	true	true	true	handle	true	18446744073709551615
handoff	handle_method	terminal	terminal	true	true	true	terminal	true	256
handoff	handle_method	terminal	terminal	true	true	true	payload_ref	true	256
cfg	value	profile	stage	false	true	false	-	-	-
close	handle_method	session	pre_window	false	true	true	handle	true	18446744073709551615
kernelcall	handle_method	backend	window	false	true	true	handle	true	18446744073709551615
kernelcall	handle_method	backend	window	false	true	true	op	true	64
kernelcall	handle_method	backend	window	false	true	true	target_kind	true	64
kernelcall	handle_method	backend	window	false	true	true	target_value	true	18446744073709551615
kernelcall	handle_method	backend	window	false	true	true	value	true	18446744073709551615
kernelcall	handle_method	backend	window	false	true	true	width	true	8
step	handle_method	backend	window	false	true	true	handle	true	18446744073709551615
step	handle_method	backend	window	false	true	true	name	true	64
log	call	backend	stage	false	true	true	level	true	16
log	call	backend	stage	false	true	true	key	true	256
```
