# CPP04–CPP06 Multicast 真机门禁（合并）

- APK：设备安装 `versionCode=193`、`versionName=1.1`；`lastUpdateTime=2026-09-17 20:00:59`
- 提交：`87f99ee`（CPP04）、`62a4641`（CPP05）、`cb35c9d`（CPP06）
- 设备/内核：5.15.189-android13-8-00016-g51bba4309aac-ab14546557（qcom/other）
- 入口：Direct；CPU main=0、consumer=1
- 温度：日志不可判定
- 结论：通过

## 产物一致性

从设备拉取 `base.apk` 后提取 `lib/arm64-v8a/libghostlock.so`，SHA-256 为
`d634883afd6a40b89945c0bc92f46f6be8d011e10f11ae15511feae3634d8ed8`，与本机构建产物
`ghostlock` 完全一致。该哈希在 CPP05 与 CPP06 构建之间相同（CPP06 只修复零调用调试表），
因此本日志同时覆盖 CPP01–CPP06 的 native 代码。

## 验证要点

- resolved profile 与 execution 参数完整，地址转换与基线一致（`init_cred alias=ffffff802abfd588`）；
- W1 首轮 page 被 `selinux_state.initialized` 字节验收拒绝，重试 1 次后通过（U01-B 页面验收策略按预期生效）；
- W1、W1b、W2、W2b、W3（TIF_SECCOMP + seccomp mode 两次写入）全部 `multicast route status=0 clean=1/1 step=0 errno=0`；
- 6 次路线均为 `calls=1 success=1` 并完成线程 join；无 fallback、无 dirty failure；
- KernelSnitch 6 次 collision（early screen 12–16/7，约 2.0s）与 6 次 mm_struct leak 全部成功；
- `child uid = 0`、seccomp probe `finit_module errno=22`、forked workers filter-free；
- root script：policy fixup rc=0、late-load exit=0、`KernelSU module loaded`、最终 `enforce=1`、`KernelSU ready`。

## 异常与未覆盖项

- `CMP_REQUEUE_PI ret=-1 errno=35`、`mcast ghost disarm ret=-1 errno=110` 与此前成功基线一致，不判定为回归；
- Resident Multicast 未启用，本日志不覆盖 resident 路径（日志不可判定）；
- `kernelsnitch_print_state` 为零调用调试入口，状态标签修复不在本日志中体现；
- 部分线程创建失败注入未执行；
- canonical/tag sweep 由 mm_struct leak 成功间接覆盖，无独立断言。

原始日志：[`CPP04-06-20260917-multicast-pass.native.log`](CPP04-06-20260917-multicast-pass.native.log)
