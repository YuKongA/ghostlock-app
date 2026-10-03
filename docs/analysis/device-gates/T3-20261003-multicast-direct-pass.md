# T3 真机门禁（native 侧）：backend 私有 section 的 StepSet（multicast_waiter）— PASS

对应提交 `0fbc0df`（T3 native；Kotlin profile-core 字段见 `e53895d`）。候选二进制 SHA-256
`9affe55240f7379a025a5136a897d2b0ffb904065f360d40c05f63674e48805f`。

## 设备与入口

- A301SO；`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`；冷启动 `boot_ms=34548`；KernelSU 未加载、Enforcing。
- profile：`/tmp/p0-steps.bin` = 仓库导出件追加 `backend.cve_2026_43499` 段 `steps=2`（W1W3）；sections 12→13。
- 入口：`GHOSTLOCK_HOME=/data/local/tmp /data/local/tmp/ghostlock-t3 --load-prebuilt-profile /data/local/tmp/p0-steps.bin`。

## 结果

PASS（完整输出见 `.native.log`）：

```
[*] multicast route status=0 clean=1/1 step=0 errno=0 attempts=16 calls=1 success=1   # x4
[+] child is root!
[*] [T+16927ms] exploit complete
[+] KernelSU ready
```

事后 `su -c id` = root；无 panic。

## 变更说明

- native：`component_ids` 加 `steps`；GLK1 v2 解析 `backend.cve_2026_43499` 段的 `steps` 键（不进 `kernel_offsets`）；
  `main.cpp` 用 `ids.steps` 构造 `ComponentSelection`。缺失/未知 StepSet → `selection_supported` 失败 → `Rejected`（fail-closed，R18）。
- Kotlin：`NativeProfileDocument.steps` + `backendSection()` + `from("backend.steps")` + `Builder` 解析（`e53895d`）。
- `cmp_disasm --reviewed` PASS（`do_one_write` 126）。

## 未完成（T3 剩余 / Kotlin）

- HOCON 未提供 `backend.steps` → `steps=0` → native 拒绝；**Kotlin 加载时需提示用户补齐**（尚未实现）。
- **Shizuku 开关 → 「一般执行 / Shizuku / UMH」三选**（用户要求）尚未实现，属 T3/B7 的 UI 部分。
