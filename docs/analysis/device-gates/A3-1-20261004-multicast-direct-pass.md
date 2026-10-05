# A3-1 真机门禁：`contract::AddressDiscovery` + kernelsnitch 可选实现 — PASS

对应提交 `2e9bc58`。冷机干净启动 → `--load-prebuilt-profile`（GLKv3 v3）。

```
[*] multicast route status=0 clean=1/1 …            # x4
[*] [spray] mm spray + kernelsnitch ready (cpu=8) +242ms
[+] child is root!
[+] KernelSU ready
```

- 事后 `su -c id` = `uid=0 … context=u:r:ksu:s0`；无 panic。
- `cmp_disasm --reviewed`：6/6 **strict IDENTICAL**（A3-1 未改活体喷雾路径与算法）。
- 白名单 3 条不变（均 `support/util.cpp→43499`，与 A3 无关）；防火墙 142 文件 / 0 stale。
- A3-2（provider 归位 `backend/.../leak/`、`utils.h` 日志拆 `support`、喷雾尾部经 `AddressDiscoveryOps` 接入）为后续批次，需完整真机泄漏门禁。
