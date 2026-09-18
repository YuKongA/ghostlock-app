# CPP12f 真机门禁：Multicast（M02 fd 收编段）— PASS

- 构建：APK 231；native `9ecc01b48761a749cbbec94716110100f478586d979ca5a60656d3c2140b0803`
- 入口：Direct；`enforce=unreadable`（完整 W1→W3 路径）
- 日志：`CPP12f-multicast-pass.native.log`（190 行）
- 结论：**PASS**

## 结果

- 6/6 route `OK clean=1/1 step=0 errno=0`；`SELinux permissive`（W1 生效）、`Write` 阶段完整。
- `exploit complete` T+28505ms、`enforce=1 (enforcing)`、`KernelSU ready`、无 panic。
- 无 `prepare_kernel_page retry`（全程 attempt=1）。

## 覆盖点（本批改动）

- `check_selinux_off`/`enforce_readable`（`UniqueFd`）：W1 前的 enforce 判定与 W3 后的 enforcing 恢复均正常。
- `perf_find_task`（`UniqueFd`+`MappedRegion`）：`perf_find_task done` 正常、child_task 可用。
- `slab_drain`（`std::array<ChildProcess,64>`）：pre-W2 drain 正常，无 fork/reap 异常。

提交前反汇编对比：攻击关键函数（waiter/owner/consumer/do_one_write/do_kernel5/run_main_route_threads/multicast workers）逐指令一致，仅 4 个外围函数增大。
