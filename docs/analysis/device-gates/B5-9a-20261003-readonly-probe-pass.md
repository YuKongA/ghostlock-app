# B5-9a 真机只读诊断（修复后）：PASS（`ready`）

对应提交 `950cbe4`（B5-9b 修复）。命令：`ghostlock --probe-cve-2026-43284 /data/local/tmp/dirtyfrag-13-5.15.ko`，退出码 **0（Ready）**。

设备：A301SO / `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`；shell uid 2000；未加载 KernelSU。
`.ko`：内置 `dirtyfrag-android13-5.15.ko` sha256 `93c5dccdce5fad99d36849148519c96d5ba135453eec3736bf2f81ac6a714cab`。
二进制：`build/native/ghostlock`（B5-9b）。

## 诊断输出（完整）

```
GLK_STATUS cve_2026_43284_diag probe
GLK_STATUS diag.device_facts present
GLK_STATUS diag.device_release 5.15.189-android13-8-00016-g51bba4309aac-ab14546557
GLK_STATUS diag.device_proc_version Linux version 5.15.189-… #1 SMP PREEMPT … has_f4c50a4=0
GLK_STATUS diag.device_selinux enforce=1
GLK_STATUS diag.device_crash_dump64 exists=1 label=u:object_r:crash_dump_exec:s0 verity=0
GLK_STATUS diag.device_vendor_candidate index=0..7 …（/vendor/lib64/libCB.so 等 8 个，same_process_hal_file）
GLK_STATUS diag.device_symbols kallsyms_restricted=1 selinux_state=0 task_defex_enforce=0 task_defex_user_exec=0 get_dc_target_dpath=0
GLK_STATUS diag.lkm_release parsed=1 android=13 kmi=5015
GLK_STATUS diag.lkm_selection matched error=None label=android13-5.15
GLK_STATUS diag.module path=/data/local/tmp/dirtyfrag-13-5.15.ko checked=1
GLK_STATUS diag.module_precheck pass error=None elf=1 modinfo=1 name=1 vermagic=1 match=1 versions_empty=1 signed=0 kcfi=1
GLK_STATUS cve_2026_43284_diag ready
```

## 结论

- 设备事实齐备：release 匹配、未打补丁（`has_f4c50a4=0`）、SELinux enforcing、`crash_dump64` label 正确且 **verity=0**、8 个 vendor 候选。
- `kallsyms_restricted=1` 为预期（未 root）；符号缺失已按 B5-9b 降级为记录项，不阻塞。
- 内置 5.15 `.ko` 预检通过：ELF64/AArch64、`.modinfo` 名称与 vermagic 齐全、**vermagic 与目标 release match**、`__versions` 空、未签名、含 kCFI。
- **诊断路径全程只读**（open/read/getxattr/FS_IOC_GETFLAGS/opendir/kallsyms），未写文件、未 fork/exec、未加载模块；43284 仍 `selection_supported=false`。

## 已知风险（进入写/触发前）

- 内置 `.ko` 与其仓内源不一致（B5 设计 §4.5/§4.6），审计建议从 `third_party/dirtyfrag/lkm/` 源自建（本机无 Docker/DDK）。
- `struct subprocess_info` 偏移、`vendor_modprobe` 域能否 `finit_module`、Defex 符号存在性均需在写/触发阶段实测。
