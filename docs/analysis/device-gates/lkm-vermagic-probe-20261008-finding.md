# 只读诊断发现 · LKM 资产 vermagic 与设备内核不符（2026-10-08 · **事实记录，无结论**）

> 分析类（≤8192 B）。**只报事实与含义**，不指认来源、不改资产（用户/Lead 口径）。

## 1. 命令与**原样**输出

```
$ adb shell '/data/local/tmp/ghostlock --probe-cve-2026-43284 /data/local/tmp/android13-5.15.ko'
module name=ghostlock vermagic=5.15.202-android13-5.15.202_r00-dirty SMP preempt mod_unload modversions aarch64
device release=5.15.189-android13-8-00016-g51bba4309aac-ab14546557 belongs=no
EXIT=4
```

## 2. 被测文件与设备

| 项 | 值 |
|---|---|
| 文件 | `build/app/lkm/android13-5.15/ghostlock.ko`（**本机 label 对应那一份**）· **23 512 B**（实测复核一致 ✓） |
| 推至设备后读取 | `/data/local/tmp/android13-5.15.ko` ✓ |
| 设备 release | `5.15.189-android13-8-00016-g51bba4309aac-ab14546557` |
| 模块自述 vermagic | **`5.15.202-android13-5.15.202_r00-dirty`** SMP preempt mod_unload modversions aarch64（`strings` 复核一致 ✓） |

## 3. 含义（如实，不拔高）

- **⚠ 更正（2026-10-08 · 真机实测推翻本节原推论）**：模块自述 vermagic = **`5.15.202-…`** 与设备 `5.15.189-…` 不同，但**真机实测该模块「已加载 → 已执行 → 已自卸载」**（E2BIG = 模块 `module_init` 的**故意**返回）⇒ **patch 级差异不阻止加载**。**机制**：Android GKI 开 `CONFIG_MODVERSIONS` ⇒ 内核 `same_magic()` 在这种模块上**跳过 vermagic 第一段（内核版本串）**，只比 tail（`SMP preempt mod_unload modversions aarch64`）；本产物带 `modversions` 且 `__versions` 段 **size = 0**（`objdump` 实测）⇒ 无 CRC 记录可比。**探测端 `belongs=no` 的出处与性质**：`core/src/main.cpp:175-182` 的 `vermagic_belongs_to(...)` 是**文本比较**（不符 ⇒ exit 4）⇒ **保守静态判据，不是内核加载裁决**。
- ⇒ **这份资产在本设备上不可用**（**与攻击链是否就绪无关** ✓）；
- 该结论由**只读诊断**在**加载任何东西之前**给出 —— 这正是 `--probe-cve-2026-43284` 的用途 ✓。

## 4. 核对结论（**kotlin-app 只读核对，2026-10-08；未改资产/脚本**）

1. **构建树 = 容器镜像自带**（与设备/档位无关）：`app/build.gradle.kts:389-401` 用 `ghcr.io/ylarod/ddk-min:<label>`（digest 已 pin：ledger `sha256:9e68cd9f…`），容器内 `make -C /opt/ddk/kdir/<label> M=/work ARCH=arm64 LLVM=1 modules` ⇒ 树 = **镜像内 `/opt/ddk/kdir/android13-5.15`**；`tools/lkm/ghostlock/Makefile:7-8` 只转发 `$(MAKE) -C $(KDIR) M=…`（**不选树、不传版本**）⇒ 该 label 产物 vermagic = **`5.15.202-…`**。
2. **vermagic 由内核构建自己写**（`modpost` 写 `.modinfo`），**全链无改写**：Makefile 13 行无版本操作；唯一后处理 = `app/build.gradle.kts:405-414` 的 `llvm-objcopy --strip-unneeded -R .comment -R .note.* -R .BTF…`，而 **`.modinfo` 不在删除列表** ⇒ 原值保留。
3. **同 label 多 release 共用一份**（**按 KMI label 8 份，无 release 维度**）：manifest 4 列 × 8 行、`KMI_COUNT = 8` 硬校验（`build.gradle.kts:230`/`:243-245`）；构建按 label 循环 ⇒ `build/app/lkm/<label>/ghostlock.ko`（实测 8 份 ✓）；运行期按 uname-r 派生 label（`LkmImageProvisioner.kt:36-43` 的 `labelFor` = `android<NN>-<major>.<minor>`）⇒ **5.15.167 / 189 / 202 都落到 `android13-5.15` ⇒ 同一份 `.ko`**。

**8 个 label 实测 vermagic（同一风险面）**：

| label | vermagic |
|---|---|
| android12-5.10 | `5.10.252-dirty` |
| android13-5.10 | `5.10.245` |
| **android13-5.15（本机）** | **`5.15.202-android13-5.15.202_r00-dirty`** |
| android14-5.15 | `5.15.202-dirty` |
| android14-6.1 | `6.1.166-dirty` |
| android15-6.6 | `6.6.127-4k-g46a034eca005-dirty` |
| android16-6.12 | `6.12.76-4k` |
| android17-6.18 | `6.18.32-4k-g47cc22554442-dirty` |

- **原结论（kotlin-app 早期读法，已更正 ✗）**：曾据静态 `belongs=no` 判「内核会以 vermagic 不符拒绝加载」⇒ **被真机实测推翻**（同 label 模块可加载可执行）；**沿革不删**，以 §4 上方「⚠ 更正」为准。
- **⚠ 该推论尚未被实测证实（2026-10-08 补）**：随后以 root `insmod` 同一 label 的模块 ⇒ **`Argument list too long（E2BIG）`**（**未出现** `invalid module format` / `version magic` / `Unknown symbol` / CRC）——见 [旧版发布真机事实](legacy-release-root-20261008-finding.md) §5。⇒ **「vermagic 不符会被拒」仍是预期**（发生在内核加载检查处），而 **E2BIG 出现在更早的参数长度检查**；二者**不予归因**（该次试载后设备重启，成因未知，未再重试）。
- **未核实（需设备侧判 · 已更正前提）**：该设备**有 KernelSU + 管理器**（`me.weishu.kernelsu` ✓，`/data/adb/ksu` root-only 存在 ✓）⇒ **决定性试载并非「没有入口」，而是「需要一次 root 授权」** ⇒ **待用户授权 root 后可执行**：设备上试载一次并看 dmesg 是否 `invalid module format / version magic`（同时可判 `ksud late-load --kmi` 是否放行）。
- **补记（归档用）**：`app/src/main/assets/lkm/` 在源码树里为空**属正常** —— LKM 资产是**生成物**（`build/app/generated/lkmAssets/lkm/<label>/`，`build.gradle.kts:37`/`:112`/`:484-497`），**打包时才并入 APK**。

## 5. 归档附注

- 8 个 label 目录均在（`build/app/lkm/<label>/`，含 `kmis.tsv`）；**`app/src/main/assets/lkm/` 目前为空**（assets 未落）——与本发现并列登记，供 kotlin-app 核验时参考。
- 相关：A6-R1 门禁记录 [a6-r1-document-to-plan-20261008-pass.md](a6-r1-document-to-plan-20261008-pass.md)（同设备、同一产物）。
