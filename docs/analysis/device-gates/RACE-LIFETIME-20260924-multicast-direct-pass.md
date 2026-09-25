# Race 生命周期与资源终结加固真机门禁（multicast_waiter，direct）— PASS

对应候选源码基线 `3bc924b1ea5d31ee75d5b6aa5e23795ff4405cb0` 加本批工作树改动。
未剥离 `build/native/ghostlock` SHA-256
`c98583ec481771926d3993d84b78c2c366d7b14e22a443e31d65c26d1e909dc2`；APK 内经
`llvm-strip --strip-all` 的同一 ELF SHA-256
`8e5ca334f17aba183a418b0a635c0a5eeb3c297d37bfd6afedb87a2b81b305ee`。

## 设备与入口

- A301SO；`uname -r` = `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`。
- 冷机 direct 入口；route = `multicast_waiter`；CPU 对固定为 `main=3 consumer=4`。
- 首轮 boot ID `f9a15813-e59c-43f5-8447-87d983ac0b26`，启动前确认
  `/sys/module/kernelsu` 不存在；自动重启后复跑 boot ID
  `51e362c4-1fff-40d5-a2fa-f220fc610463`，启动前再次确认 KernelSU 未加载。

## 结果

- 首轮在 W2 route 已报告 `route_done status=0 clean=1/1`、ghost disarm 和 `threads joined` 后自动重启；
  日志 SHA-256 `c7a5e9f75e871bced91ff9117e09d9c35237c2e3605f75efd0cc5ec06c5cc6b2`。设备未授权
  shell 读取 pstore，因此不把重启归因到某一源码行；阶段符合已知 `KERNEL-PANIC-01` 波动区间。
- 同一候选、同一 CPU 对的冷机复跑 **PASS**：W1/W1b/W2/W2b/W3 的每次 route 均为
  `status=0 clean=1/1`，每次均先 ghost disarm 再 `threads joined`；随后 `child is root!`、
  `[T+21694ms] exploit complete`、handoff 成功、`KernelSU ready`，native 正常退出。
- PASS 日志 SHA-256
  `35b54c89d36712b43c089ad7cb8afe763f698158c8e5fd019594481e3317b0f8`，归档于设备
  `Download/GhostLock/20260924-232524/ghostlock-direct-0.log.txt`。
- 本次没有触发 `route_done` deadline、join 失败或 pre-arm rollback；正常路径没有出现新增
  terminal dirty 日志。timeout fault gate 未注入，fail-stop 的内核侧安全性仍不作声明。

## 反汇编证据

- 基线 `/private/tmp/ghostlock-b4-review-p1b-candidate` SHA-256
  `cacc2c6c7530710d6ec0ec7085452389fe58f6dfd0a7e286948fdf517e5040f9`。
- `owner_thread`、`waiter_thread`、`consumer_thread`、`do_kernel5_fake_lock_route`、`do_one_write`
  仅地址/layout shift；指令形状不变。
- `run_main_route_threads` 的差异仅增加 monotonic deadline、reset/join 结果检查与 fail-stop 错误分支；
  正常调用顺序仍是 create workers → requeue/route → stop → waiter/owner/consumer join。
- `multicast_owner_worker`、`multicast_waiter_worker` 的差异是 pre-arm stop 检查及 waiter futex deadline；
  正常顺序仍是 lock2 → lock1 → wait-requeue → socket/stamp → ghost disarm → unlock → close。
- 资源闭包中 `MulticastWaiterRoute::write`、`multicast_waiter_adjust`、`multicast_waiter_stamp` 严格一致；
  `w1_resident_repair`、`w2_fast_repair_prebuild`、`cleanup_page_prepare_state` 仅 layout shift。
  `stop`/`resident_write` 增加 rollback、join 检查和 fail-stop 分支；`prepare_good_kernel_page` 的 199 个
  调用目标与顺序完全一致。
