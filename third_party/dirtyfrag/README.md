# third_party/dirtyfrag —— 上游参考源码（vendored）

> 目的：为 CVE-2026-43284 backend（B5）提供**可审计、可复现**的上游源码，供逐文件改造进本项目结构。
> 本目录**不参与构建**（无 Makefile/CMake 引用），仅作参考与迁移来源。

## 来源与 commit（2026-10-03 抓取）

| 来源 | 用途 | commit |
|---|---|---|
| [ankitrawatgit/DirtyFrag-Android-Root-Jailbreak](https://github.com/ankitrawatgit/DirtyFrag-Android-Root-Jailbreak) | 主线（IpSec + crash_dump + libc++ sentry + LKM/UMH） | `de2ab7b` |
| [diabl0w/DFRoot](https://github.com/diabl0w/DFRoot) | LKM/UMH 上游（含 `soft_reboot`） | `3050d5b` |
| [polygraphene/DFReroot](https://github.com/polygraphene/DFReroot) | 最小 LKM（仅清 SELinux），其 `.ko` 与源一致 | `9edc769` |
| [lsposed/lspromise](https://github.com/lsposed/lspromise) | 页缓存写原语（`exp.c`/`stage1.S`/`splicehelper.c`） | `0258165` |
| [combeng6th/DirtyInit](https://github.com/combeng6th/DirtyInit) | 无特权 XFRM/IpSec 用户态思路 | `3409c35` |

**授权**：上述仓库**均未提供 LICENSE 文件**（默认保留所有权利）。本目录仅作本地研究/迁移参考，
不改变上游权利；对外分发前须逐一取得授权或替换。迁移进 `src/core/backend/cve_2026_43284/` 的代码需在本项目内重写并保留署名。

## 内容

- `lkm/ankit/`、`lkm/dfroot/`、`lkm/dfreroot/`：LKM 源码 + Makefile + `build.sh`（DDK：`ghcr.io/ylarod/ddk-min:<kmi>`，8 KMI）。
- `usermode/ankit/`：主线用户态（`exp.c`、`aes256.h`、`hmac_sha256.h`、`libcxx.S`、`elf_parser.c`、`include.inc`、`reporter.h`）。
- `usermode/lspromise/`：页缓存写原语与 `shelld`/`splicehelper` 源码。
- `usermode/dirtyinit/dfi_exploit.c`：无特权 IpSec 注入参考。
- `app-reference/`：上游 Java 编排（`ExploitRunner.java`、`MainActivity.java`）。

## 构建（在有 Docker + DDK 的机器上；本仓不集成）

```sh
cd third_party/dirtyfrag/lkm/dfroot
./build.sh              # 逐 KMI 用 ghcr.io/ylarod/ddk-min:<kmi> 构建 dirtyfrag-<kmi>.ko
```

产物须与真机 `uname -r` 逐字匹配的 vermagic；未签名、`__versions` 空、含 kCFI；`struct subprocess_info`
偏移与符号 hook 未参数化（按 KMI 编译决定）。真机可加载性属 B5 设备门禁。

## 迁移映射（→ 本项目）

| 上游 | 目标位置 |
|---|---|
| `lkm/*/dirtyfrag.c` | `src/core/backend/cve_2026_43284/lkm/`（重写为可审计实现） |
| `usermode/ankit/exp.c` 页缓存写 | `src/core/backend/cve_2026_43284/pagecache/`（适配 `FileCacheWriteOps`） |
| `usermode/*/splicehelper.c` | `src/core/backend/cve_2026_43284/pagecache/splicehelper` |
| `usermode/dirtyinit/dfi_exploit.c` IpSec | `src/core/backend/cve_2026_43284/ipsec/` |
| `app-reference/ExploitRunner.java` | Kotlin `app/.../data/` + `terminal/umh_forward` 编排 |
