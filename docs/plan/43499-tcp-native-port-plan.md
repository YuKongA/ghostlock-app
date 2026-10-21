# 43499 tcp_zerocopy native port plan

Status: planning only. No code change.

> **2026-10-21 指针**：批准状态见 [43499-approval-status.md](43499-approval-status.md)（唯一权威，本件不再自行声明）；
> 执行结构（PipelineEngine 唯一权威 / Stage 与 KtStep 只是 node 形态 / 会话 close 与执行 close 互斥）见
> [execution-architecture.md](execution-architecture.md)。
> 本件 **planning only**：无 route 实现；状态表 §4 记为**待批准**。

## Source of truth

remote/main: `src/core/route/tcp_zerocopy_route.cpp` (365 lines), `tcp_zerocopy_route.h`.
It uses:
- `race::PiRace` with `consumer_go/calls/success/inflight` atoms;
- `profile::TargetProfile` (`page_size`, heap page base/fake_lock/fake_fops);
- `support::UniqueFd` / `MappedRegion` / `disable_rseq_for_thread`;
- loopback TCP pair + 16 MiB memfd + punch thread;
- `getsockopt(TCP_ZEROCOPY_RECEIVE)` with zc[0x28]=task, zc[0x30]=lock.

Current dev-nocpp has no equivalent `tcp_zerocopy_route`; `access_tcp_zerocopy.hpp` only refuses via `arm_route()==false`.

## Required deltas

1. `PiRace`: current class has no consumer atoms. Either add a route-local consumer controller or extend PiRace with the same controller shape; no second engine.
2. `TargetProfile`: current route config is explicit; TCP needs page_size, fake_task, fake_lock, fake_fops, mapping_length, CPU/nice/policy, attempts.
3. Resources: current tree has raw fd/fields, no `UniqueFd/MappedRegion`; port must own fd/memfd/mmap and close/join on every path.
4. Punch thread: implement bounded start/stop/join; `disable_rseq_for_thread` equivalent is missing.
5. `getsockopt(TCP_ZEROCOPY_RECEIVE)`: create loopback pair, zerocopy receive request, zc words; exact result reporting.

## Proposed JNI

`NativeTcpZerocopyJni.execute(params: ByteArray, page: ByteArray): ByteArray?`
- fixed params header + page side buffer;
- result: status/step/errno, route_won, calls/successes, page send/readback fields, punch_phase/error, cleanup flags.

## Ownership

- JNI owns listener/client/server/punch/memfd/mmap for one call.
- Page is caller-owned; route never closes it.
- Stop: set punch_stop + disarm consumer, join bounded; failure marks dirty and retains.

## Gates

- host: codec/bounds, failure paths with fake socket/fallocate;
- AVD: JNI load, loopback pair, punch thread start/stop, no panic; route success not assumed;
- device: cold boot, fixed CPUs, one route, dmesg, uid evidence.

## Why not copy

Direct copy fails on `profile`, `PiRace` atoms, `UniqueFd/MappedRegion`, global session, and `pr_*` logging. It is a port, not a file move.
