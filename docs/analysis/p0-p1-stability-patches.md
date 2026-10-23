# P0/P1 稳定性补丁应用记录（2026-10-22）

## 已应用

### P0：fd sentinel 完整化

- `glk_session_create_43499` 的 `runtime_init`：
  `reclaim_sv/quarantined_reclaim_sv/prebuilt_reclaim_sv = -1`，
  `memfd_leak/mr_fd = -1`。
- `prepare_ctxs` 的 `mm_ctx.memfds` 分配后全部填 `-1`。
- `close_ctx_memfds`、`memfd_leak` 关闭条件改为 `>= 0`。
- 这修复了：
  - W1 `scratch page quarantine failed`；
  - W2 prebuild 立即失败；
  - W2 只跑 1 轮。

### P0：W2 失败路径

- prebuild/stash 失败：不再 `return 0`，继续本轮 `do_one_write` + verify。
- activation 失败：`discard_prebuilt_page`，跳过 W2b，继续 verify。
- repair 失败：停止本轮 W2，日志
  `W2 fast repair route failed; stopping W2 attempts`。
- `discard_prebuilt_page` 清空全部 `prebuilt_page_base/fake_*`。

### P0：KernelSnitch

- 已回移 `aaaa6503`：possible CPU → 2048 表；
- 已回移 `f8a50857`：state 表补 `collisions not found`；
- 保留 `scan_limit` 边界；
- `kernelsnitch_setup` 拒绝 `mm_struct_sz=0`、`thread_cnt=0`、
  `collision_cnt<2`，避免零步长/空碰撞死循环。

### P1：W3 leaf probe

按 `43238c52`：

- leaf direction probe 失败不再假设 `[target+8]` 盲写；
- 输出 `retiring child` 并 `continue`，下一轮 park 旧 child、spawn 新 child。

## 未应用

- `993f399e` compact select 新页重试：当前 C 架构由外层
  `retry_write_stage` 重新 prepare，需单独确认是否等价。
- `452bd1fd` 泄漏 walk 的逐段推进/防 spin：已加 setup 参数守卫，
  但未移植 C++ 的 walk 测试。
- `c6776ad` 43284 bounded verify：43284 C 路径没有同名 `write_image_verified`，
  需单独映射。
- `396e52d` 的 `.ghostlock_iomem` 测量未移植；当前用 `IDENTITY_END` 封顶。

## 门禁

```text
./gradlew :app:externalNativeBuildDebug --offline  # 0 warnings
host hash test: pair 2048=1874 4096=3922/1874, rc=0
flatten S1: 19/19 PASS
flatten S4: 17/17 PASS
ghostlock43499 b/B=0
ghostlock43284 b/B=0
```

## 设备状态

- 2026-10-22 冷机复跑 **PASS**：
  `W1b repaired → W2 prebuilt → W2b repair → child is root → KernelSU ready`；
  设备未 panic，bootreason `reboot,shell`。
- 记录：[20261022-cold-43499-pass.md](device-gates/20261022-cold-43499-pass.md)。
- 单次 PASS；长期成功率仍需多轮冷机统计。

## 文档

- [C/C++ 稳定性差异](c-to-cpp-stability-gap.md)
- [KernelSnitch 历史审计](kernelsnitch-history-audit.md)
- [fd sentinel 运行记录](device-gates/20261021-2102-sentinel-fix-partial.md)
