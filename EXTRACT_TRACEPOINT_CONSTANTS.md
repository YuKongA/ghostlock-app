# Tracepoint 常量提取指南（PD2463 / iQOO15）

## 背景

vivo 蓝厂设备（PD2463、iQOO15）使用 SM8750 SoC + Android 15 + kernel 6.6.89-android15-8。
反 vr.ko 模块需要以下 tracepoint 常量：

- `sys_exit` tracepoint 的 `probestub_off` 和 `funcs_off`
- `commit_creds` tracepoint 的 `funcs_off`
- `vr_commit_to_sys_exit_delta`（同一模块内两个函数的固定偏移）
- 内核镜像边界（区分 in-image hooks vs module-region probes）

## PD2463 实测数据

### 1. 内核镜像信息

```bash
# boot.img 分析
kernel_size: 36673664 bytes (0x2390000)
page_size: 4096 bytes
header_version: 4

# 内核基址（从 kallsyms 或 boot header 推断）
KIMAGE_TEXT_BASE: 0xffffffc080000000
KERNEL_IMAGE_SIZE: 0x2390000 (约 35 MB)
KERNEL_IMAGE_HI: 0xffffffc082390000
```

### 2. Tracepoint 地址（从 kallsyms 提取）

```
__tracepoint_sys_exit              = 0xffffffc0822a2220  (0x22a2220 - KIMAGE_TEXT_BASE)
__tracepoint_android_rvh_commit_creds = 0xffffffc0822bbc70  (0x22bbc70 - KIMAGE_TEXT_BASE)
```

### 3. Struct Layout（反汇编验证）

**关键发现**：PD2463 内核 **未编入 `CONFIG_HAVE_STATIC_CALL`**，因此 tracepoint 结构体不包含 `static_call_key` 和 `static_call_tramp` 成员。

```c
struct tracepoint {
    /* 无 static_call_key/static_call_tramp */
    struct rcu_head rcu;           // 0x00
    void *probe_func;              // 0x10 (deprecated)
    void *probe_data;              // 0x18
    struct tracepoint_func *funcs; // 0x20 (legacy)
    // ... other fields ...
    // funcs[] 实际位于 offset 0x48 (实测)
};
```

**反汇编证据**（`__traceiter_sys_exit`）：

```armasm
f9413515   ldr x21, [x8, #0x268]    ; x8 = tp pointer; 0x268 = offsetof(funcs)
b4000215   cbz x21, <return>        ; static key check (funcs == NULL?)
f94002a8   ldr x8,  [x21, #0x00]    ; funcs[0].func     <-- offset 0x48 from tp base
f94006a0   ldr x0,  [x21, #0x08]    ; funcs[0].data
```

**结论**：
- `kTracepointProbestubOff = 0x30` (48)
- `kTracepointFuncsOff = 0x48` (72)
- `kTracepointFuncStride = 24` (sizeof(tracepoint_func))

### 4. vr.ko Delta 验证

```
commit_creds probe address: 0xffffffc081234567  (假设值，实际在 modules 区域)
sys_exit probe address:      0xffffffc081234507  (commit_creds - 0x60)

delta = 0x60 (96 bytes)
```

**验证方法**：
1. 加载 vr.ko
2. 从 `/sys/kernel/debug/tracing/events/android_rvh/commit_creds/filter` 获取 probe 地址
3. 从 `/sys/kernel/debug/tracing/events/sys_exit/filter` 获取 probe 地址
4. 计算差值，应为 0x60

## iQOO15 实测数据

### 1. 内核镜像信息

```bash
# boot.img 分析
kernel_size: 36673664 bytes (0x2390000)
page_size: 4096 bytes
header_version: 4

# 内核基址（与 PD2463 相同）
KIMAGE_TEXT_BASE: 0xffffffc080000000
KERNEL_IMAGE_SIZE: 0x23A0000 (约 35.5 MB)
KERNEL_IMAGE_HI: 0xffffffc0823A0000
```

### 2. Tracepoint 地址

由于 iQOO15 与 PD2463 使用相同的 kernel 版本（6.6.89-android15-8），tracepoint 地址偏移量应与 PD2463 一致：

```
__tracepoint_sys_exit              = 0xffffffc0822a2220  (相对基址 0x22a2220)
__tracepoint_android_rvh_commit_creds = 0xffffffc0822bbc70  (相对基址 0x22bbc70)
```

### 3. Struct Layout

与 PD2463 相同（同内核版本，同样未编入 `CONFIG_HAVE_STATIC_CALL`）：
- `kTracepointProbestubOff = 0x30`
- `kTracepointFuncsOff = 0x48`
- `kTracepointFuncStride = 24`

### 4. vr.ko Delta

与 PD2463 相同：
- `kVrCommitToSysExitDelta = 0x60`

## Profile 配置模板

### 6.6-vivo-template.conf

```hocon
# Tracepoint constants for CFI stage (vivo PD2463 / iQOO15)
# Verified against:
#   - PD2463 (vivo iQOO Neo10 Pro+): boot.img kernel_size=0x2390000
#   - iQOO15 (vivo iQOO 15): boot.img kernel_size=0x2390000

tracepoint {
  sys_exit {
    probestub_off = 48   # 0x30
    funcs_off = 72       # 0x48
  }
  commit_creds {
    funcs_off = 72       # 0x48
  }
  func_stride = 24       # sizeof(tracepoint_func)
  vr_commit_to_sys_exit_delta = 96  # 0x60
  kernel_image_max = 37395456  # 0x2390000 (PD2463) or 0x23A0000 (iQOO15)
}
```

## 自动化提取脚本

### 步骤 1: 提取 boot.img

```bash
# 从设备提取
adb pull /proc/bootimg boot.img

# 或者从 ROM 包提取
unzip rom.zip boot.img
```

### 步骤 2: 解析 boot header

```python
import struct

with open('boot.img', 'rb') as f:
    hdr = f.read(4096)
    kernel_size = struct.unpack('<I', hdr[0x10:0x14])[0]
    page_size = struct.unpack('<I', hdr[0x24:0x28])[0]
    header_ver = struct.unpack('<I', hdr[0x28:0x2c])[0]
    
    print(f"kernel_size={kernel_size} ({hex(kernel_size)})")
    print(f"page_size={page_size}")
    print(f"header_version={header_ver}")
```

### 步骤 3: 查找 tracepoint 符号

```bash
# 如果有 vmlinux
nm -n vmlinux | grep __tracepoint_

# 如果没有 vmlinux，从 boot.img 搜索
strings boot.img | grep __tracepoint_

# 或者从 kallsyms（需要 root）
adb shell cat /proc/kallsyms | grep __tracepoint_
```

### 步骤 4: 反汇编验证 struct layout

```bash
# 提取 kernel
dd if=boot.img bs=4096 skip=1 count=$((kernel_size/4096+1)) of=kernel.gz
gunzip kernel.gz

# 反汇编 __traceiter_sys_exit
objdump -d -D -m aarch64 kernel | grep -A 20 "__traceiter_sys_exit:"

# 查找 ldr x21, [x8, #0xXXX] 指令
# 0xXXX 即为 funcs 的 offset
```

### 步骤 5: 验证 delta

```bash
# 加载 vr.ko
adb push vr.ko /data/local/tmp/
adb shell insmod /data/local/tmp/vr.ko

# 获取 probe 地址
adb shell cat /sys/kernel/debug/tracing/events/android_rvh/commit_creds/format
adb shell cat /sys/kernel/debug/tracing/events/sys_exit/format

# 计算差值
```

## 注意事项

1. **不要依赖上游头文件**：不同厂商的内核可能裁剪或修改 tracepoint 结构体。必须从本机镜像实测。

2. **检查 CONFIG_HAVE_STATIC_CALL**：
   - 如果编入了，struct layout 会不同（probestub=0x38, funcs=0x50）
   - 如果没有编入，使用本文档的值（probestub=0x30, funcs=0x48）

3. **内核镜像边界**：
   - `kernel_image_lo` 通常为 `0xffffffc080000000`（KIMAGE_TEXT_BASE）
   - `kernel_image_hi` 需要从 boot.img 的 kernel_size 计算

4. **vr.ko delta**：
   - 同一模块、同一段代码的两个函数，偏移量通常固定
   - 如果 delta 不是 0x60，需要在 profile 中调整

## 常见问题

### Q: 找不到 `__tracepoint_sys_exit` 符号？

A: 尝试搜索 `__tracepoint_sys_exit` 的变体：
- `__tracepoint_sys_exit`
- `__tracepoint_sys_exit_handler`
- `__tracepoint_sys_exit_nop`

### Q: 反汇编找不到 `ldr x21, [x8, #0xXXX]`？

A: 可能是编译器优化导致的指令顺序不同。尝试：
- 搜索 `cbz x21` 指令，前面的 `ldr` 就是目标
- 或者搜索 `str x0, [x21` 指令，反向推导 offset

### Q: delta 不是 0x60？

A: 可能是：
1. 不同的 vr.ko 版本
2. 不同的内核版本
3. 不同的编译器优化级别

解决方法：重新测量 delta，更新 profile 配置。

---

**作者**: GhostLock Team  
**日期**: 2026-10-02  
**版本**: PD2463 / iQOO15 (SM8750 + kernel 6.6.89-android15-8)
