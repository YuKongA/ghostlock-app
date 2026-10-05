# contract/pipeline 身份词汇拆分 真机门禁（43499 multicast）— PASS

对应提交 `4a06c66`（身份词汇 `pipeline`→`contract`，防火墙白名单 16→2）。冷机干净启动 → `--load-prebuilt-profile`。

```
[*] multicast route status=0 clean=1/1 …            # x4
[+] child is root!
[+] KernelSU ready
```

- 事后 `su -c id` = `uid=0 … context=u:r:ksu:s0`；无 panic。
- cmp：候选 vs 批前 HEAD `git worktree` 构建 **strict 6/6 IDENTICAL 且 sha256 相同**（`91f7492b…`）→ 零机器码影响；B0 已刷新为该值。
- 布局/wire/golden/Kotlin 对拍、`session_layout_test`（base=104/state=1560）均通过。
