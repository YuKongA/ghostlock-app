# B5-9a 真机只读诊断：BLOCKED（`SelinuxStateMissing`）— 根因与修复项

对应提交 `f05b613`（B5-9a）。命令：`ghostlock --probe-cve-2026-43284 /data/local/tmp/dirtyfrag-13-5.15.ko`，退出码 **2（DeviceBlocked）**。

设备：A301SO / `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`（shell uid 2000）。
`.ko`：内置 `dirtyfrag-android13-5.15.ko` sha256 `93c5dccdce5fad99d36849148519c96d5ba135453eec3736bf2f81ac6a714cab`。

## 诊断输出（节选）

```
GLK_STATUS diag.device_facts unavailable error=SelinuxStateMissing
GLK_STATUS diag.device_release 5.15.189-android13-8-00016-g51bba4309aac-ab14546557
GLK_STATUS diag.device_selinux enforce=1
GLK_STATUS diag.device_crash_dump64 exists=1 label=u:object_r:crash_dump_exec:s0 verity=0
GLK_STATUS diag.device_symbols selinux_state=0 task_defex_enforce=0 task_defex_user_exec=0 get_dc_target_dpath=0
GLK_STATUS cve_2026_43284_diag device_blocked
```

## 根因

`platform::collect_device_facts`（`device_facts.cpp:71-74`）把 **kallsyms 的 `selinux_state` 符号存在性**列为**致命**（`DeviceFactError::SelinuxStateMissing`）。
未 root 时 `/proc/kallsyms` 受限（kptr_restrict），shell 读不到任何符号 → 所有符号 0 → fail-closed → device_blocked。
同组的 Defex 符号缺失仅为**记录项**（非致命），`selinux_state` 却致命——不一致，且与链条不符：SELinux 由 LKM 关闭，native 前置阶段不需要该符号。

## 修复项（B5-9b 前置）

把 `selinux_state` 缺失降级为**记录项**（与 Defex 一致），保留 `release`/`proc_version`/`crash_dump64`/`vendor_candidates` 等真前置为致命；
同步更新 `cve_2026_43284_diagnostic_test` 与 `backend_terminal_test` 的期望；随后重跑本诊断。

## 已可确认的设备事实

- release 与 profile 匹配；`has_f4c50a4=0`（未打补丁）；SELinux enforcing；
- `crash_dump64` 存在、label `u:object_r:crash_dump_exec:s0`、**verity=0**；
- `/vendor/lib64` 候选 8 个（`libCB.so` 等，label `same_process_hal_file`）；
- 诊断路径只读，未写文件、未 fork/exec、未加载模块；43284 仍 `selection_supported=false`。
