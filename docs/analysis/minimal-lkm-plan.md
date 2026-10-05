# 最小内核侧载荷（GhostLock LKM）设计与验证计划

> 状态：**实施中**（L 级：新增内核侧组件 + 攻击链终态）。上游对应物为 DFRoot 的
> `dirtyfrag.ko`（已 vendored 于 `third_party/dirtyfrag/lkm/dfroot/`，仅作参考实现，不直接使用）。

## 1. 为什么需要它，以及为什么自建

43284 链的原语只给「文件页缓存写」——没有 uid 0、没有内核内存写。要从「改了内存里几个字节」
变成 root，必须在**内核上下文**做四件事（详见 `third_party/.../dirtyfrag.c` 与设计评审）：

| # | 动作 | 必要性 |
|---|---|---|
| 1 | 经 kprobe 取 `kallsyms_lookup_name`，解析任意内核符号 | 所有后续符号查找的前提（该符号未导出） |
| 2 | `WRITE_ONCE(*selinux_state, false)` 置 permissive | 否则 init 域的后续动作被 SELinux 拦 |
| 3 | （可选）kprobe 三星 `task_defex_enforce`/`task_defex_user_exec` 强制放行 | 仅三星机型需要 |
| 4 | `call_usermodehelper` 执行 `/system/bin/sh -c "<cmd>"`，并强制 `subprocess_info->path` 以绕过 `CONFIG_STATIC_USERMODEHELPER_PATH=""` | 从内核拉起用户态 root 工具（`ksud late-load`） |
| 5 | `return -E2BIG` 使模块**自卸载** | 不留驻 |

**自建理由**：上游发布的 `.ko` 与公开源码不一致（不可审计），且本机拿到的镜像 vermagic 为
`5.15.202-android13-5.15.202_r00-dirty`，与设备 `5.15.189-android13-8-00016-…` 不符
（`CONFIG_MODULE_FORCE_LOAD` 未设 → 需改写 vermagic 才能加载）。自建可获得
**可审计（源码在库内）+ 尺寸更小 + 参数化**的模块。

## 2. 实现（`tools/lkm/ghostlock/`）

- `ghostlock.c`：上面 1–5 步；**模块参数**：
  - `cmd`（charp，默认惰性 `/system/bin/true`，长度上限 512）——真正执行什么由加载方决定；
  - `permissive`（int，默认 1）——是否置 SELinux permissive；
  - `defex`（int，默认 0）——是否挂三星 Defex（符号缺失仅记录，不致命）。
- **fail-closed**：`cmd` 非法、`kallsyms_lookup_name` 不可用、`selinux_state`/`call_usermodehelper_*`
  缺失 → **在任何状态改动之前**返回错误并自卸载；每步 `pr_info` 便于真机核验。
- `Makefile`：`obj-m`、`-Os -fno-asynchronous-unwind-tables -fno-unwind-tables`（体积优先）。
- `build.sh`：用 DDK 容器构建（`ghcr.io/ylarod/ddk-min:android13-5.15`，与上游同源），再用
  `llvm-objcopy` 做 size diet（`--strip-unneeded` + 去掉 note/BTF/hyp 段），产物
  `tools/lkm/ghostlock/out/ghostlock-android13-5.15.ko`（**不入库**，`build/` 与 `out/` 均忽略）。

## 3. 构建与 KMI

- 设备内核：`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`（GKI 5.15 分支）。
- GKI 保证**同一 KMI 内符号 CRC 稳定**，因此按 `android13-5.15` 构建即可；唯一差异是 vermagic
  字符串（release 与 `-dirty` 等），由链侧 `--cve43284-allow-vermagic-rewrite` 在加载前改写
  （已实现并测试，默认关闭）。
- 尺寸目标：与上游同量级（≤ 约 8 KiB），以控制页缓存写块数与时间（16B/块）。

## 4. 验证计划

| 批 | 内容 | 判定 |
|---|---|---|
| **LKM-1** | 源码 + 构建脚本 + 本计划；本地构建出 `.ko` | 构建成功；`modinfo` 可见 name/description/vermagic；sha256 记录 |
| **LKM-2** | 真机**惰性加载**：`insmod ghostlock.ko cmd="/system/bin/touch /data/local/tmp/glk_lkm_ok"` | `dmesg` 出现 4 步日志；marker 文件出现；`lsmod` 无残留（自卸载）；SELinux 变 permissive（`getenforce`）→ 记录并在测试后 **重启复原** |
| **LKM-3** | 接入 43284 链：载体写入 → `insmod` → `cmd="<ksud> late-load …"` | `/proc/modules` 出现 `kernelsu`；`su` 可用；AVB/文件/verity 检查通过 |
| **LKM-4** | 与上游 `.ko` 行为对照（可选） | 同场景下 marker/日志一致 |

**回滚**：模块自卸载；SELinux permissive 与页缓存改动均**重启复原**；测试前后跑
`tools/avbcheck` + `avb_guard.sh check-all`。

## 5. 风险

- **R-1 `subprocess_info` 布局**：`info->path` 依赖该结构体字段偏移；若内核启用
  `CONFIG_RANDSTRUCT`（GKI 默认不启用），偏移会不同 → 需在真机验证（marker + dmesg）；不成立则改用
  其他绕过 `STATIC_USERMODEHELPER` 的方式。
- **R-2 kprobe 可用性**：若 `kallsyms_lookup_name` 无法用 kprobe 定位（如 KPROBES 关闭），模块
  直接 fail-closed（不影响主链，链会报 LKM 阶段失败）。
- **R-3 KMI 不符**：CRC 冲突时内核拒绝加载并打印 `disagrees about version of symbol`；届时需按设备
  精确内核重建（Sony 开源内核）。
- **R-4 SELinux 状态**：permissive 会持续到重启；测试后必须记录并重启复原。
- **R-5 未签名模块**：设备 `CONFIG_MODULE_SIG_FORCE` 未设 → 允许（内核会 taint），已确认。

## 6. 与平台对策插件化的关系

本模块属于 ABI 中**故意不提供**的 `KERNEL_HOOK` 能力范畴（用户态阶段回调无法做到），
走**内核侧通道**；`ghostlock.c` 即该通道的最小实现，后续若需三星 Defex/RKP 类扩展，在此模块内参数化。
