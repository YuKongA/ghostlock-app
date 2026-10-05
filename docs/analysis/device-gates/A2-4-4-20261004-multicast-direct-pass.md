# A2-4-4 真机门禁：中性数据词汇 → contract/model.hpp（43499 multicast）— PASS

对应提交 `e549eeb`。冷机干净启动 → `--load-prebuilt-profile`（GLKv3 v3）。

```
[*] multicast route status=0 clean=1/1 …            # x4
[+] child is root!
[+] KernelSU ready
```

- 事后 `su -c id` = `uid=0 … context=u:r:ksu:s0`；无 panic。
- **二进制 sha256 与批前 HEAD 完全相同**（`91f7492b…`）→ 零机器码影响；cmp 6/6 IDENTICAL。
- 布局冻结：`kernel_offsets` 520/8、`TargetProfile` 784/8、`session_layout_test` base=104/state=1560。
- 防火墙白名单 2→3（accessors 迁出 profile 产生的 `support/util.cpp→backend_profile/accessors.hpp`；待逐文件去耦）。
