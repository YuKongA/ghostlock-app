# A2-4-3 真机门禁：platform::abi owner schema + bind_all 合并（43499 multicast）— PASS

对应提交 `9ceef1c`。冷机干净启动 → 43499 入口 `--load-prebuilt-profile`（GLKv3 v3）。

```
[*] multicast route status=0 clean=1/1 …            # x4
[+] child is root!
[+] KernelSU ready
```

- 事后 `su -c id` = `uid=0 … context=u:r:ksu:s0`；无 panic。
- `cmp_disasm --reviewed` PASS（6/6 OPERAND-SHIFT 数据位移；新增 constexpr schema 表致 rodata 位移，批前 `git worktree` 构建对 B0 为 strict IDENTICAL，已记录理由）。
- 布局冻结：`kernel_offsets` 520/8、`TargetProfile` 784/8、`session_layout_test` base=104/state=1560；wire 不变；Kotlin `ProfileManifest{,V3}AgreementTest` 绿（仅 owner 列变化）。
- 防火墙白名单 18→16（清除 `profile/accessors.hpp`、`profile/runtime_struct_offsets.h` → backend 两条）。
