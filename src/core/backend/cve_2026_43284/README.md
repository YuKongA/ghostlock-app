# cve_2026_43284 backend

CVE-2026-43284 page-cache-write backend. 逐文件上游映射、分批计划与运行时通道决策见
[docs/archive/20261007-2237-cve-2026-43284-refactor-plan.md](../../../../docs/archive/20261007-2237-cve-2026-43284-refactor-plan.md)；
设计见 [cve-2026-43284-b5-design.md](../../../../docs/analysis/cve-2026-43284-b5-design.md)。

## 状态（截至 B5-9c）

| 批次 | 内容 | 状态 |
|---|---|---|
| B5-1 | 运行时会话秘密帧（通道 B，84B BE + 4B 长度前缀） | ✅ 实现 + host 测试（仅 43284+frame 路径读，fail-closed） |
| B5-2 | `ipsec/`：AES-256 ECB/CBC、HMAC-SHA256、ESP 布局/ICV/verify、CBC-IV | ✅ FIPS-197 / SP800-38A / RFC 4231 KAT |
| B5-3 | `pagecache/`：`write16`/块序列 + 可注入 `splice_io` + `FileCacheWriteOps` | ✅ host 测试（fake splice/pipe） |
| B5-4 | `lkm/`：KMI/vermagic fail-closed、`.ko` 选择/交付、UMH 命令构造 | ✅ host 测试（KMI 表/预检/argv 结构+错误路径）；真加载需设备 |
| B5-5 | `steps/` ELF+Hook（`.dynsym`、trampoline/BTI/PAC、shellcode） | 🔶 host 部分实现 + 测试（只读 ELF、hook 规划、BTI/PAC 策略、shellcode 参数化）；真 patch 属 B5-6/设备 + 目标 `.so` |
| B5-6 | `steps/` chain（crash_dump 桥、vendor 载体回退、patch 校验、trigger） | ⬜ 需设备 |
| B5-7 | `backend_terminal` + `platform`：`BackendExecution<B,Input>`、`UmhForwardInput` 填充、设备事实探测 | ⬜ 部分需设备 |
| B5-8 | 组合：catalog/orchestrator/pipeline + `umh_forward` + 43499 回归 | ⬜ |
| B5-9a | 只读真机诊断入口（`--probe-cve-2026-43284`）+ 只读 ChainOps 绑定 | ✅ 真机 PASS（A301SO / android13-5.15） |
| B5-9b | 受限 kallsyms / selinux_state 缺失降级为记录项，不再秒退 | ✅ 修复 + 真机 PASS |
| B5-9c | 真实 ChainOps 绑定 + 分阶段执行入口（`--run-cve-2026-43284`） | 🔶 实现 + host 测试；真机分阶段验证待做 |
| B5-9 | 真机门禁（逐 KMI）+ `.ko` 审计 + 归档 | ⬜ 需 `.ko` + 设备 + ksud |

`contract::backend_available(Cve2026_43284)` 仍为 **false**；未过真机前不得标 supported。

## 布局

| Path | 内容 |
|---|---|
| `session_frame.hpp/.cpp` | 会话语密钥帧（通道 B）编解码/校验/清零 |
| `ipsec/ipsec.hpp,.cpp` | `IpsecSaParams`、ESP 尺寸/组包/ICV/verify、`compute_cbc_iv`、`zeroize_bytes` |
| `ipsec/aes256.hpp,.cpp` | AES-256 ECB/CBC（Odzhan BSD-3 署名） |
| （HMAC-SHA256 已上移） | S4 R8：`ipsec/hmac_sha256.*` 与 `plugin/sha256.*` 合一为 `support/sha256.hpp,.cpp`（Odzhan BSD-3 署名保留） |
| `pagecache/pagecache.hpp,.cpp` | `PageCacheWriteContext`、`write16`/`write_block`/`read_block`、`make_file_cache_write_ops` |
| `pagecache/splice_io.hpp,.cpp` | 可注入 syscall 面（`pipe2`/`splice`/`vmsplice`/…）+ `real_splice_io()`（`__linux__`） |
| `lkm/lkm_policy.hpp`、`lkm/lkm_image.hpp` | KMI/vermagic/`.ko` 解析与 UMH late-load 命令（B5-4） |
| `steps/elf_hook.hpp,.cpp` | 只读 ELF 解析/符号定位/PT_LOAD/.text/.rela 校验 + hook site（BTI/PAC 策略），纯 byte-span（B5-5） |
| `steps/shellcode.hpp,.cpp` | shellcode 槽位参数化（MOVZ/MOVK、branch 编码）+ trampoline patch 描述（不写文件，B5-5） |
| `steps/steps.hpp` | PageCacheWrite step-set 壳（不含 pipeline，ADR-0004 R1） |
| `real_ops.hpp,.cpp` | 真实 ChainOps 绑定：pagecache write（real_splice_io）+ pread 读 + double-fork trigger + LKM/UMH 终态探测 + release 清零；`run_ready()` 要求全绑定且 DeviceProbeOps 可用 |
| `stage_runner.hpp,.cpp` | `--run-cve-2026-43284` 分阶段入口：plan/write/trigger/full + 结构化状态记录；host 可注入 fake ops |
| `schema.hpp`、`glkv3_schema.hpp` | 43284 私有 GLK section 的 v2/v3 schema |

设备侧未验证项：splice 页是否真被共享并就地解密、`.ko` 真加载（vermagic/modversions/未签名/kCFI）、`crash_dump64`/vendor 载体/Defex 符号。
