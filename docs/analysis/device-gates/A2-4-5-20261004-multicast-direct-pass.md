# A2-4-5 真机门禁：AddressSpace 解耦 TargetProfile（43499 multicast）— PASS

对应提交 `46224d8`。冷机干净启动 → 43499 路径入口 `--load-prebuilt-profile`（GLKv3 v3 profile）。

## 结果

```
[*] multicast route status=0 clean=1/1 …            # x4
[+] child is root!
[+] KernelSU ready
```

- 事后 `su -c id` = `uid=0 … context=u:r:ksu:s0`；无 panic；完整日志见 `.native.log`。
- `cmp_disasm --reviewed`：6 函数 **IDENTICAL (strict)**（69/892/177/417/224/126）。
- `kernel_offsets`/`TargetProfile` 布局、`session_layout_test`（base=104/state=1560）、wire 均未动。
- R1：`memory/` 不再 include `profile/` 或 `backend/`（`grep` = 0）。
