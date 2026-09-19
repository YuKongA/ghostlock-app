# S14 Multicast 真机门禁

- 设备/版本：A301SO，`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`
- 入口/路线：Direct，one-shot Multicast Waiter
- CPU：main 0，consumer 1；KernelSnitch 运行于 CPU 8
- 结果：通过，完整执行至 `KernelSU ready`
- 原始日志：`S14-20260913-multicast-pass.native.log`

## 执行证据

- W1 首次写入成功；private scratch repair 首次成功并释放 quarantine。
- W2 credential 首次写入成功，fast repair 完成，victim UID 验证为 0。
- W3 两次写入均首次完成，`finit_module` probe 返回正常 errno 22，确认 seccomp filter 绕过。
- 每次 Multicast 路线均报告 `status=0 clean=1/1 step=0 errno=0 calls=1 success=1`，PI 三线程均完成 join。
- KernelSU late-load 首次成功，最终恢复 enforcing 并输出 `KernelSU ready`。
- 本次日志未记录温度，温度条件不可判定；未发生运行时回退。

## 回归结论

S13 的通用 one-shot context 改写改变了该敏感原语的行为，连续复测表现为路线 syscall 成功但 W1 未生效。恢复专用小栈帧、payload builder、socket 与 drain/close 顺序后真机通过。Resident Multicast 继续使用 `MulticastWaiterRouteContext`；one-shot 保留专用实现并通过统一 `RouteStatus` 接入 S14 控制器。
