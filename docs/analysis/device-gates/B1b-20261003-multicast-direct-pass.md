# B1b 真机门禁：中性 TerminalInput / RootProgram（multicast_waiter）— PASS

对应提交 `69913f3`（43284 计划 B1b）。候选二进制 SHA-256
`8d89405b4a33221ec9830ab407682b534c9fecaac4c0bab6984f61de798b1c99`。

## 设备与入口

- A301SO；`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`；冷启动 `boot_ms=34885`；
  KernelSU 未加载、Enforcing、uid 2000。route = multicast_waiter；CPU 对 0/1。
- 入口：`GHOSTLOCK_HOME=/data/local/tmp /data/local/tmp/ghostlock-b1b --load-prebuilt-profile
  /data/local/tmp/p0.bin`。

## 结果

PASS（一次冷机跑通过；完整输出见 `.native.log`）：

```
[*] multicast route status=0 clean=1/1 step=0 errno=0 attempts=16 calls=1 success=1   # x4
[+] child is root!
[*] [T+20494ms] exploit complete
[+] KernelSU ready
```

事后：`kernelsu ... (OE)`；`su -c id` → `uid=0(root)`；无 panic。

## 变更说明

新增中性 `terminal::RootProgram`（kind + 有界 argv，host-safe、可平凡拷贝）与 `terminal::TerminalInput`
（携带 `RootProgram`）；`RootedChild` 派生 `TerminalInput`，`UmhForwardInput` 同基。
`RootedChild` 生产 handoff 结构变化为**加法**（新增基类字段），终端读取 `pid/command/uid_read/flags` 的逻辑不变。
`cmp_disasm --reviewed` 6 函数骨架一致（`do_one_write` 126）。D4 方向已定：root 程序会话单值、App 选择、
`root_child` 与 `umh_forward` 两条路径都可启动、`RootedChild` 支持自定义程序+参数。
