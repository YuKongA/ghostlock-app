# A301SO CVE-2026-98260 exec_mmap 前置门（r2）

> 日期：2026-11-03（America/Toronto）  
> 分支/HEAD：`dev-nocpp` / `b119085915748a940740d7ce919b5a30ed8e9346`  
> 设备：A301SO / `5.15.189-android13-8-00016-g51bba4309aac-ab14546557` / USB `QV770MFGJ1`  
> 结论：**不是 offset 失败；A301SO 缺少 6.1+ 触发所需的 killable mmap lock。real/fake/KSU 不可进入。**

## 1. 根因

当前 locker/SIGKILL trigger 需要 exec 在 `de_thread()` 已改变 PID 语义之后、
`posix_cpu_timers_exit()` 清理 timer RB tree 之前失败。主线 6.1、6.6、6.12 的
`exec_mmap()` 调用 `mmap_read_lock_killable(old_mm)`；fatal signal 可让它返回错误，形成该窗口。

A301SO exact boot image 的匹配源码和重建 vmlinux 均证明其 5.15 `exec_mmap()` 使用
`mmap_read_lock(old_mm)`。反汇编 `exec_mmap=0xffffffc0085631f8`：

```text
0xffffffc008563288  bl rwsem_read_trylock
0xffffffc008563290  ldr x1, [sp]
0xffffffc008563294  mov x0, x22
0xffffffc008563298  mov w2, #0x2
0xffffffc00856329c  bl rwsem_down_read_slowpath
```

该等待不响应当前 trigger 的 SIGKILL；锁释放后 exec 继续到
`posix_cpu_timers_exit()`，victim node 被正常摘除。timer slab 随后仍会释放并被 socket spray
取回，所以“同 PFN 命中”可以出现，但 reap 时已不存在 stale node，写原语不会发生。

已独立验证且不是本次失败根因：CVE 修复顺序未回移、`k_itimer=0x108`、node `+0x78`、
cred uid `+0x4`、order-1/31 objects、`rb_erase` 目标零写路径。

## 2. 单变量真机证伪

诊断版只增加“受控 PFN 命中后让 child 读取 uid；uid 未变则对同一 real cred 重试”，其余
trigger、offset、spray 与 ELF 配置不变。ELF SHA-256：
`7d87e935e821ca6c5c43a4591e97f9df48ffef942b6ae3fe4ed602bb81e9bfd6`。

```text
boot_id=d645130c-9cd4-4567-84d5-13b09ecb17a2
SELinux=Enforcing; KSU_UNLOADED
plant 1: order-1 PFN hit=1; child uid/euid=2000/2000
plant 2: order-1 PFN hit=1; free_seen=1; child uid/euid=2000/2000
plant 3: order-1 PFN hit=1; child uid/euid=2000/2000
[!] real-cred write unverified after controlled retries -- parking
```

这把“偶发 reclaim miss”与“没有 stale node”分开：三次受控回收都没有写，不能继续把失败归因于
竞态运气或偏移。随后强制重启；新 boot id `f8e6da01-f73d-4764-b951-6e2ab1a8dd58`，
`Enforcing`、`KSU_UNLOADED`、无 helper 残留。

## 3. 最终 fail-closed 门

参数权威新增 `GLK98260_EXEC_MMAP_HAS_KILLABLE_MMAP_LOCK=0`，revision `a301so-515-r2`。
最终 ELF SHA-256：`e11c8c4d9f2440ae8ba9f5adef9cbe032157cbed28c13cadae957e4775db3acf`；
host/device 回读一致。

```text
command: /data/local/tmp/gl98260-a301so-r2-final
input: no arguments; cold boot; shell uid 2000; Enforcing; KSU_UNLOADED
result: ids PASS; base PASS; cred leak PASS;
        [prereq] FAIL exec_mmap uses non-killable mmap_read_lock;
        6.1+ locker/SIGKILL trigger is incompatible with this kernel
exit: 2
post: GL98260_ABSENT; Enforcing; KSU_UNLOADED; no stale-node plant ran
```

## 4. 推进边界

该结论不等于“所有 5.15 都不可利用”：若 vendor 回移 killable lock 或存在另一个可控的
post-`de_thread` / pre-`posix_cpu_timers_exit` 失败点，仍可重新评估。A301SO 下一变量只能是等价失败窗口；
继续调 offset、spray 数量或 fake/KSU 参数不会补上缺失的 control-flow prerequisite。

只读源码勘察得到一个未验证候选：5.15 `coredump_wait()` 在共享 mm 上设置 `mm->core_state` 并杀死
其它 mm sharer；exec 随后取得 non-killable read lock 会因 `core_state` 返回 `-EINTR`，位置仍早于
timer cleanup。设备 shell 的 core soft limit 为 0、hard limit 为 unlimited，soft limit 可提升；
`core_pattern` 对 shell 不可读。该候选改变 trigger 与共享 mm 生命周期，不属于 offset 参数化，当前
没有运行，也没有据此放开 fail-closed 门。

