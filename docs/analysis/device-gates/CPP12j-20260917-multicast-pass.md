# CPP12j 真机门禁：Multicast（CORE / mm_struct_sz 宏替换）— PASS

- 构建：APK 240；native `cea1bedc5f085778489bff88d86893c6ab5bd643230764b899d196fbacfc9172`
- 入口：Direct（untrusted_app uid=10510）；`enforce=unreadable`（完整 W1→W3）
- 设备：A301SO / 5.15.189-android13-8-00016-g51bba4309aac-ab14546557；CPU main=0 consumer=1
- 日志：`CPP12j-20260917-multicast-pass.native.log`（229 行）
- 结论：**PASS**，与 `CPP12i`（native `a7e1ea23…`）行为等价

## 覆盖点

- 8/8 route `status=0 clean=1/1 step=0 errno=0 calls=1 success=1`；8 次 spray 全部 `prepare_kernel_page ok attempt=1`；`threads joined` ×8。
- W2 首轮随机未命中（route+W2b 后 `child uid = 10510`），attempt 2/15 的 route+W2b 后 `child uid = 0`、`child is root!`——与 CPP12i 相同的竞态随机性，未出现状态误判。
- W1b `private scratch repaired; releasing quarantine`；W3 两次 route 后 `child seccomp filter bypassed (finit_module errno=22)`。
- 交接：`enforce=1 (enforcing)`、`KernelSU ready`；`exploit complete` T+35886ms（8 次 route）。
- 无 fallback、无 dirty failure、无 panic。

## 本次收编范围

- 删除 `common.h` 的 `CORE` 与 `mm_struct_sz()` 宏：调用点改用 `runtime_config_snapshot().main_cpu` 与新的 `target_profile_mm_struct_sz()` 访问器；KernelSnitch init 显式接收 pin CPU，不再读取进程全局；删除两个零调用旧入口 `kernelsnitch`/`kernelsnitch_param`（提交 `e9b8151`）。
- 主机测试 `profile_test` 增加 mm_struct stride 的回退向量（profile 值 / 零字段 / 未加载 / 空指针）。
- 提交前反汇编对比：8 个攻击关键函数**严格逐指令一致**；641/642 个函数形状一致，唯一差异是 `prepare_good_kernel_page` 的寄存器调度；**无任何地址注解位移**（代码/数据布局未变）。

## 不可判定项

- 设备温度未记录（日志不可判定）；resident 路线未启用。
