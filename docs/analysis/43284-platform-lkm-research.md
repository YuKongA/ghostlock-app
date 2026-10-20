# 43284 平台对策移出中继 LKM：调研与设计

- 日期：2026-10-19
- 用户裁定：payload 脚本自由；manager 查找/加载脚本由 Kotlin 生成；LKM 只执行。
- 调研：DFRoot@c274137、DirtyFrag@de2ab7b、vivo vr.ko。

## 机制

### OPPO/OnePlus

DFRoot 用 module_kallsyms_lookup_name 找：

~~~text
oplus_exe_block_ret_handler
oplus_root_check_post_handler
~~~

再 patch 成返回 0。纯内核函数 patch，脚本不能替代。

### Samsung DEFEX

函数：task_defex_enforce、task_defex_user_exec、get_dc_target_dpath。

DirtyFrag 用 kprobe pre_handler：regs[0]=0、pc=lr，跳过函数体。

仍是内核代码，脚本不能替代。

### vivo VRKO

vr.ko 是 permanent 厂商模块：

~~~text
android_rvh_commit_creds：给 uid0 进程写 task tag
__tracepoint_sys_exit：进程退出时 tag 非零且 euid0 就 kill
~~~

公开研究：rmmod 不可用；PI-write 不能改 .text；可改 tracepoint funcs/static_call key，但需要内核读写。

## 可行性

| 平台 | 移到用户 LKM | 结论 |
|---|---|---|
| OPPO/OnePlus | 高 | LKM 可做同样 patch |
| vivo VRKO | 中高 | LKM 可中和 tracepoint；必须早于 manager |
| Samsung DEFEX | 低/部分 | 可放 LKM，但 DEFEX 可能阻止其加载 |

## 约束

1. 用户 LKM 仍需 insmod；模块签名/KMI 可能拒绝。
2. relay 必须先 permissive，UMH 才能执行脚本。
3. DEFEX 若拦 insmod/exec，存在鸡生蛋。
4. VRKO 必须在 manager/ksud 前中和。

## 架构

~~~text
relay.ko 最小：permissive + UMH sh run.sh
run.sh Kotlin 生成：insmod platform.ko + payload + manager 最后
platform.ko 用户/设备加载：OPPO/DEFEX/VRKO 对策
~~~

LKM 不查 manager、不拼脚本、不选平台对策。

## DEFEX 例外

若 DEFEX 阻止 platform.ko 加载，relay 保留最小 defex_bootstrap=1，只 patch DEFEX 函数；其余移出。


## DEFEX“部分移出”的准确含义

- **不能移出**：让 `insmod platform.ko` 有机会执行的 DEFEX 放行原语。
- **可以移出**：设备/型号相关的 DEFEX 符号选择、额外检查、per-task 策略、审计与恢复。

### 留在 relay.ko

```text
defex_bootstrap=1 时：
  kprobe task_defex_enforce     -> regs[0]=0, pc=lr
  kprobe task_defex_user_exec   -> regs[0]=0, pc=lr
```

只做“让 platform.ko 能加载/执行”的最小放行，不包含 OPPO/VRKO。

### 移到 platform.ko

```text
get_dc_target_dpath 等设备相关符号
型号/版本分支
per-task/cred 策略
额外 DEFEX 检查
审计、恢复、失败回滚
```

### 时序

```text
relay init
  -> permissive
  -> defex_bootstrap 放行
  -> UMH 执行 run.sh
       -> insmod platform.ko
       -> payload / manager
  -> UMH 返回
  -> relay 卸载（kprobe 自动注销）
platform.ko 在 relay 卸载前已驻留，继续持有自己的 kprobe。
```

### 为什么不能全部移出

DEFEX 可能拦截：

```text
insmod
execve
```

如果 relay 不放行，platform.ko 根本加载不了，脚本也跑不到“insmod platform.ko”这一行。  
所以 DEFEX 的“加载前放行”必须留在 relay；其余策略可以移到 platform.ko。

## 结论

- OPPO 可以移出。
- VRKO 可以移出，但需早加载与 KMI/签名验证。
- DEFEX 不能无条件移出，保留最小 bootstrap 作为 fallback。
