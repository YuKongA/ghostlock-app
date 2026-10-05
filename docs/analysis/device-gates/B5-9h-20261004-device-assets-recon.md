# B5-9h 设备侧资产侦察（只读，2026-10-04）

设备：A301SO / `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`，Android 15，slot `_a`，KernelSU 已加载（用于 `su` 读取）。

## 1. hook 目标：`/system/lib64/libc++.so`

- 存在：1083168 B，sha256 `794eb8fafd7be35da3725e9ec0b15189c6f4f2544f5b78afd8a647dde5b69195`（与 `files-baseline` 一致）。
- 目标符号 `_ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE6sentryC1ERS3_`（C2 同址）**存在**，VMA `0xb70a8`。
- **前导指令**：
  ```
  b70a8: d503245f  bti c        <-- entry guard
  b70ac: f9400028  ldr x8, [x1]
  b70b0: 3900001f  strb wzr, [x0]
  b70b4: f9000401  str x1, [x0, #0x8]
  ...
  b70d4: d503233f  paciasp      <-- 函数体内（非紧邻入口）
  ```
- 全库 2151 处 BTI/PAC → 必须按上游 **PACIASP/BTI +4 规则**处理；本仓已实现 `HookGuardPolicy::{Reject, SkipGuard}`（常量 `kAarch64BtiC=0xD503245F`/`kAarch64Paciasp=0xD503233F`）。
- **结论**：`hook_guard` 应选 **SkipGuard（上游 +4）**——跳过 `bti c` 后在 +4 打补丁；`bti c` 保留即满足 BTI 间接调用落地要求；`paciasp` 在函数体内，需 shellcode 恢复时保持签名/认证配对（由既有 hook 规划的 displaced-slot 机制处理）。

## 2. 备选目标（DirtyInit 式）：`/system/lib64/libbase.so`

- `_ZN7android4base10LogMessage*` 系列符号存在；三个候选前导均为 **`paciasp` 在 +0**，随后 `stp x29,x30`。
- 若 libc++ 路由不可用，可换 `libbase.so` 的 `LogMessage`（同样落在上游 +4 规则内）。

## 3. vendor carrier（patch #2 目标）

| 上游默认候选 | 本机 |
|---|---|
| `/vendor/lib64/libbinderdebug.so` | **PRESENT** |
| `/vendor/lib64/libstagefrighthw.so` | **PRESENT** |
| `/vendor/lib64/libstagefright_aidl_bufferpool2.so` | ABSENT |
| `/vendor/lib64/libbsp_module.so` | ABSENT |

## 4. patch #1 目标

`/apex/com.android.runtime/bin/crash_dump64` 存在（482904 B，`root:shell 0755`）。

## 5. 代码侧缺口（B5-9h-1）

链的 hook 已是**可注入**的（`real_chain_apply_hook` 需要 `libcxx_image`/`hook_guard`/`hook_io`/`hook_symbol`，默认符号 `kLibcxxSentrySymbol`），但 **staged 真机入口尚未把这些设备资产接进来**：
- 需要：hook 目标路径（默认 `/system/lib64/libc++.so`）、符号（默认 sentry）、`hook_guard` 策略、carrier 路径（与 patch #1 目标**分开**）、patch #1 目标路径。
- 之后即可在真机跑 `--stage=plan`（**只计算不写字节**）验证 hook 区域/steal 字节/`bti c` 处理与 shellcode 落位。

## 6. 真机 `--stage=plan` 结果与**阻塞性发现**（2026-10-04，B5-9h-1）

提交 `a014581` 后真机只读运行：

```
GLK_STATUS run.cve_2026_43284 stage=plan
GLK_STATUS run.module path=/data/local/tmp/dirtyfrag-13-5.15.ko bytes=11168 wrote=0 verified=0
GLK_STATUS run.hook hook_target=/system/lib64/libc++.so hook_guard=skip hook_vma=0x0 …
              shellcode_len=0 trampoline_len=16 hook_error=PlanFailed
GLK_STATUS run.cve_2026_43284 failed error=HookPlanFailed
```

只读成立（`wrote=0 verified=0`）；hook 规划失败。**host 探针**用真机 `libc++.so` 字节逐级复现：

```
find_symbol ok value=0xb70a8 size=100
locate ok hook_vaddr=0xb70ac displaced=0xf9400028 guard=0xd503245f skipped=1 payload_max=0
plan FAILED err=7 (PlanFailed)
```

即：符号定位与 `bti c` +4 规则**完全正确**，失败在 **shellcode 落点无空间**（`payload_max=0`）。

### 根因（上游算法 + 本机 ELF 布局）

- 上游 `elf_parser.c:47`：`*payload_target = phdr.p_offset + phdr.p_filesz` —— 落点 = **首个可执行 PT_LOAD 的段尾**。
- 本机 `libc++.so` 可执行 LOAD：`p_offset=0x84d60 p_filesz=p_memsz=0x798c8`（**段尾零余量**），且 `.plt` 正好顶到段尾（`0xfe628`）。
- 扫描 init 使用的 **16 个库**，全部 `memsz - filesz == 0`，段内最大零洞 **≤12 B**：

| lib | filesz | memsz-filesz | 最大零洞 |
|---|---|---|---|
| libbase.so | 125216 | 0 | 8 |
| liblog.so | 37104 | 0 | 12 |
| libutils.so | 52192 | 0 | 12 |
| libcutils.so | 40960 | 0 | 12 |
| libprocessgroup.so | 218480 | 0 | 8 |
| libselinux.so | 66208 | 0 | 8 |
| libbinder.so | 373408 | 0 | 12 |
| libc++.so | 497864 | 0 | 12 |
| libm.so | 146816 | 0 | 8 |
| libz.so | 64768 | 0 | 8 |
| libjsoncpp.so | 113040 | 0 | 4 |
| libui.so | 186944 | 0 | 12 |
| libhidlbase.so | 344864 | 0 | 12 |

**结论：上游 DirtyFrag 的「段尾放 shellcode」方案在本机（Android 15 / 该固件布局）整体不可行**，且不存在 ≥480 B 的代码洞。

### 候选替代落点（待裁决，未实施）

1. **牺牲一个 init 在触发窗口内不会调用的函数**：在同库可执行段内选一个 ≥480 B 的导出函数覆盖之（仅页缓存、重启复原）。需静态与实测证明触发窗口内 init 不调用它。
2. **跨库落点 + 动态寻址**：让 libc++ 的 16B trampoline 经 **GOT** 取某符号运行时地址、加固定 delta 跳到另一个库中牺牲函数的时隙（两库同属 init，delta 与 ASLR 无关）。仍需一个能牺牲 ≥480 B 的库。
3. **缩小 payload**：不可行（最大洞 12 B）。
4. **延长文件**：页缓存无法为超过 EOF 的偏移供页（`splice` 无页），不可行。
5. **放弃本机 43284 真机链**：本机已有 43499 可用；43284 保持「已接线未可用」。

> 无论选哪条，`plan` 都必须能**证明**落点安全（不覆盖被调用代码、不跨重定位、16 对齐），否则 fail-closed。
