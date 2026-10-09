# 真机门禁记录 · A6-R1（**文档 → 计划**，2026-10-08 · PASS）

> 分析类（≤8192 B）。**本轮证明的是「文档 → 计划」可用，不是「攻击可用」**（执行被如实拒绝）。

## 1. 设备与产物身份

| 项 | 值 |
|---|---|
| 设备 | **`A301SO`** · Android 15 · CPU **6 核** |
| 内核 | `5.15.189-android13-8-00016-g51bba4309aac-ab14546557` |
| SELinux | **Enforcing** |
| KernelSU | **已安装** ✓ · 管理器包 **`me.weishu.kernelsu`** ✓ · **`/data/adb/ksu` 存在但 root-only** ✓ · 无特权 shell **无 root** ✓（`su` 不在 PATH、`/system/bin/su` 不存在 —— 这是 KernelSU 的正常形态：root 由管理器按应用授权）；另 **`moe.shizuku.privileged.api`（Shizuku）** 亦在 ✓ |
| 权限 | **未 root**（shell uid，无 su） |
| 产物 | `build/native/ghostlock` · **1 760 256 B** · ELF **aarch64 PIE** · arm64 交叉构建（NDK **28.2.13676358**）· **零警告** |
| 部署 | 推至 `/data/local/tmp/ghostlock` · 权限 **755** |

## 2. 输入文档（两份，均经 **4 字节大端长度前缀**送入 stdin / `--ghostlock-app-call`）

| # | 来源 | 大小 | 身份 | 备注 |
|---|---|---|---|---|
| ① | `profile-core/src/test/resources/device-document-fixture.hex` | **1991 B** | sha256 **`53bb9a384641a7d7922800f367f0840c7ef085e76aa1917699392c88861126dc`** | **14 段**；由 App 链在**设备档 + 显式 43499 选择**下产出 |
| ② | `build/profiles/5.15.189-android13-8-00016-g51bba4309aac-ab14546557.bin` | **202 B** | sha256 前缀 **`bafb0784…`** | 声明**默认组合**的 Gradle 导出 |

## 3. 真机输出（**原样**）

```
$ adb shell '/data/local/tmp/ghostlock --ghostlock-app-call < /data/local/tmp/doc.framed'
plan chain=mcast_rootchild steps=w1,w2,w3 source=queue
executor-unavailable
EXIT=4

$ adb shell '/data/local/tmp/ghostlock --ghostlock-app-call < /data/local/tmp/doc43284.framed'
plan chain=umh steps=pagecache_write source=queue
executor-unavailable
EXIT=4
```

## 4. 判据与含义（如实）

- ① **两条链的步骤均由文档携带的 queue 选出** ✓（输出 `source=queue`，**非回退**）；
- ② **执行被如实拒绝** ✓（`executor-unavailable` ⇒ 机制**尚未在本树实现**）⇒ **本轮证明「文档 → 计划」可用**；
- ③ 主机侧同输入检查亦在门禁内：`make -C core plan-check` ✓。

## 5. **未验证什么**（边界，不得拔高）

- **攻击执行**：`executor-unavailable` ⇒ 未验证任何写原语/竞争/接管（本轮**不涉及**）；
- **App 端到端 `profile.bin` 路径**：仅以固定件与 Gradle 导出为输入，**未跑** App 全链路；
- **真机上的计划校验门禁全量**：仅验证到「计划产出 + 执行拒绝」，未覆盖 §3.2 全部拒绝码路径。

## 6. 归档附注

- 设备日志按项目惯例落 `Download/ghostlock-debug-log/<时间>/`（本轮以 adb 原样输出为准，见 §3）。
- 本记录与 [UML](../development/full-process-uml.md) §1 的新入口 IPO（**文档入 → 计划出 → 如实拒绝**）**同批更新**。
