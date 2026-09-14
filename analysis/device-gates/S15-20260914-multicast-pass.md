# S15 最终真机回归：Multicast 通过

- 时间：2026-09-14 10:35（America/Toronto）
- 设备/内核：A301SO，`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`
- 应用：重新部署的 `versionCode=176`，对应 S15 `0c47a9f`
- 入口：Direct；CPU：main 0 / consumer 1
- 路线：Multicast waiter；TCP、Select 与运行时回退未启用，日志不可判定
- 温度：日志未记录，无法判定

## 结果

- W1 首个 Heap 页面因目标字节为偶数被验收策略拒绝，第 2/4 次准备成功；路线写入随后使 SELinux permissive。
- W1b、W2、W2b、W3 TIF_SECCOMP 和 seccomp mode 均在首轮完成。
- 六次 Multicast 路线执行均返回 `status=0 clean=1/1 step=0 errno=0 calls=1 success=1`，并完成线程 join。
- W2 后 child UID 为 0；W3 的 `finit_module` 探针确认 filter-free worker。
- root 脚本完成策略修复和模块加载，最终输出 `KernelSU ready`，SELinux 恢复 enforcing。
- 日志未出现 dirty route、运行时回退、卡死或异常清理。

原始日志：[S15-20260914-multicast-pass.native.log](S15-20260914-multicast-pass.native.log)
