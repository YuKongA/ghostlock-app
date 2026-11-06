# W2 CPU affinity 恢复计划（2026-10-25）

English | [English](w2-cpu-affinity-plan.md)

## 当前状态与证据

最新设备日志为：
`/storage/emulated/0/Download/ghostlock-debug-log/20261025-173702/ghostlock-direct-0.log.txt`。
W1 完成，W2 启动，随后反复出现
`sched_setaffinity(...): Invalid argument`，native 以 `255` 退出，尚未到达 W3。
此前 `20261025-173314` 已到达 W3，并报告
`child seccomp filter bypassed (finit_module errno=22)`，因此本批次不修改 seccomp。

疑似失败点是 KernelSnitch 的 affinity reset：`reset_cpu_pin()` 向
`sched_setaffinity()` 传入全 bit mask，而不是进程实际允许的 mask。当前选择的
CPU pair `4,5` 也没有在 worker 创建前按 live affinity mask 校验。

## 目标与约束

- 保持 W1/W2/W3 顺序；除 affinity 设置/恢复外不改变攻击窗口机器码形状。
- 只恢复当前进程允许的 CPU，不替换成任意 CPU。
- 除非最小 affinity helper 改动确有必要，不修改 `kernelsnitch/`。
- 不修改 profile offset、route 参数、payload 布局、seccomp 写入或 handoff。

## 计划改动

1. 在 KernelSnitch pin scan thread 前保存 caller 的 allowed CPU mask。
2. 使用保存的 mask 恢复，而不是构造全 bit mask。
3. native 执行前按 live affinity mask 校验 main/consumer CPU；不允许时记录明确
   的 profile/configuration error 并退出。
4. 为 mask 校验与恢复增加 host tests。

## 验证

- `make -C src native-host-tests`
- NDK 零警告构建
- `make -C src lint-tidy`
- 使用已确认 baseline 对八个攻击函数执行 `tools/cmp_disasm.py`
- 同内核/profile 冷机复跑并记录 W2/W3 结果

## 明确保留

不修改 W1/W2/W3 target address、write mode、route、victim 生命周期、seccomp offset
或 KernelSU handoff。

## 状态

- [x] 读取最新设备日志并与此前成功运行比较。
- [x] 确认 W3 之前的 W2 affinity 失败。
- [x] 实现 KernelSnitch 的已保存 affinity 恢复。
- [x] 执行可用 host 检查；完整 native/NDK 与设备验证仍待执行。
