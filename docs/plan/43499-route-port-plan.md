# 43499 route port plan (N3 page consumer) — revision 2

Status: design-critic approved revision 7 with **no blocking issues** (2026-10-20). **PI 例外已获用户批准（2026-10-20）→ 逐项状态见 [43499-approval-status.md](43499-approval-status.md) §4；route 可写码，不再等任何 PI/fallback 决定。** 但**页面落点尚未证明**：无内核页读回、无 `victim uid=0`，`requeue=1` 不等于页被消费 —— 未过 N5 不得标 supported。

> **2026-10-21 指针**：批准状态见 [43499-approval-status.md](43499-approval-status.md)（唯一权威，本件不再自行声明）；
> 执行结构（PipelineEngine 唯一权威 / Stage 与 KtStep 只是 node 形态 / 会话 close 与执行 close 互斥）见
> [execution-architecture.md](execution-architecture.md)。
> 本件是 **multicast 生产 route 的在办计划**；PI 例外见状态表 §4。

## 1. Scope

- 43499 multicast-waiter route only; not generic.
- Kotlin owns: target/value/mode/offset, stamp/page tuple, page bytes, attempt/retry/success, uid gate.
- C++ owns: thin syscall wrappers + actor batch that executes the fixed order; no profile reads, no retry decisions, no uid judgement.
- UI/config wiring remains out of scope.

## 2. Legacy anchors (read-only reference)

- execute/guard/build_stamp: landing/multicast_route.hpp:279-314, :363-375.
- start_threads: landing/multicast_route.hpp:390-430.
- requeue (caller thread): :566-619.
- waiter park/trigger/ghost disarm: :697-746.
- owner target->chain: :747-790.
- consumer setattr/fallback lock: :791-845.
- socket/setsockopt: landing/multicast_route_linux.hpp:138-147.
- PI prohibition: docs/analysis/pi-window-constraints.md:60-64; legacy consumer `Ops::setattr` is **inside** its `PI-IN-CONSUMER` section and is the measured panic path. This route does not claim otherwise; it needs an explicit exception or fallback-only approval.

## 3. Actor model and exact order

Three actors + caller requeue, matching legacy:

| actor | work |
|---|---|
| consumer | wait consumer_go; setattr(waiter_tid, nice, policy); fallback lock_pi_until if rejected; increment calls/success |
| owner | lock_pi(target_word); owner_started=1; wait waiter_ready; lock_pi(chain_word); owner_chain_done=1; hold until stop |
| waiter | lock_pi(chain_word); waiter_ready=1; wait owner_started/waiter_ready; park WAIT_REQUEUE_PI(wait_word, target_word); on return trigger(); close socket; route_done=1; unlock chain; wait owner_chain_done; expired LOCK_PI ghost disarm |
| caller (JNI thread) | wait waiter_waiting/owner_started; wait waiters bit; CMP_REQUEUE_PI(wait_word,target_word); wait route_done; join |

Trigger, only on waiter thread after park returns:

1. open_datagram socket;
2. build stamp (zero, family word at +8, fake_task/fake_lock, tuple);
3. setsockopt(MCAST_BLOCK_SOURCE, stamp);
4. if stamped, consumer_go = attempt;
5. wait consumer_success;
6. close socket;

After trigger returns, waiter order is exactly: `route_done=1` -> `unlock_pi(chain)` -> wait `owner_chain_done` -> expired `LOCK_PI` ghost disarm. The disarm is NOT inside trigger and NOT before owner-chain completion (legacy :697-722).

No socket is opened before the waiter parks; Kotlin does not call setsockopt.

## 4. Params / result

NativeRouteParams fixed 192-byte layout (little endian; JNI also receives `page: ByteArray` and `stamp: ByteArray` side buffers):

| off | type | field |
|---|---|---|
| 0 | u32 | magic 0x474C5250 |
| 4 | u16 | version 1 |
| 6 | u16 | header 192 |
| 8 | u32 | total 192 |
| 12 | u32 | reserved 0 |
| 16 | i32 | page_fd |
| 20 | u32 | page_len |
| 24 | u32 | stamp_len |
| 28 | u32 | flags |
| 32 | u64 | chain_word |
| 40 | u64 | wait_word |
| 48 | u64 | target_word |
| 56 | u64 | target |
| 64 | u64 | value |
| 72 | u32 | mode 1=Zero, 2=Credential |
| 76 | u32 | preserve_child 0/1 |
| 80 | u32 | stamp_waiter_offset |
| 84 | u32 | stamp_task_offset |
| 88 | u32 | stamp_lock_offset |
| 92 | u32 | max_stamp |
| 96 | u64 | fake_task |
| 104 | u64 | fake_lock |
| 112 | i32 | consumer_nice |
| 116 | u32 | consumer_policy |
| 120 | u32 | consumer_delay_us |
| 124 | u32 | max_calls |
| 128 | u32 | burst_calls |
| 132 | u32 | fallback_lock_us |
| 136 | u32 | arm_sequence |
| 140 | u32 | max_attempts |
| 144 | i32 | waiter_cpu |
| 148 | i32 | owner_cpu |
| 152 | i32 | consumer_cpu |
| 156 | u32 | settle_us |
| 160 | u32 | hold |
| 164 | u32 | join_timeout_ms |
| 168 | u32 | page_timeout_ms |
| 172 | u32 | page_waiter_offset |
| 176 | u32 | page_task_offset |
| 180 | u32 | page_lock_offset |
| 184 | u32 | page_slot_size |
| 188..191 | — | reserved 0 |

NativeRouteResult fixed 128-byte layout:

| off | type | field |
|---|---|---|
| 0 | u32 | magic 0x474C5252 |
| 4 | u16 | version 1 |
| 6 | u16 | header 128 |
| 8 | u32 | total 128 |
| 12 | u8 | ran |
| 13 | u8 | status |
| 14 | u8 | refusal |
| 15 | u8 | page_stage |
| 16 | u32 | page_bytes |
| 20 | i32 | page_errno |
| 24 | u32 | actors_started |
| 28 | u32 | flags |
| 32 | i32 | waiter_result |
| 36 | i32 | owner_result |
| 40 | i32 | requeue_result |
| 44 | i32 | consumer_result |
| 48 | i32 | stamp_result |
| 52 | i32 | stamp_errno |
| 56 | u32 | calls |
| 60 | u32 | successes |
| 64 | u32 | route_done |
| 68 | u32 | join_timeout |
| 72 | i32 | disarm_result |
| 76 | i32 | disarm_errno |
| 80 | u32 | stamp_equal |
| 84 | u32 | socket_closed |
| 88 | u64 | elapsed_ns |
| 96 | u32 | page_slot_equal |
| 100 | u32 | page_timeout |
| 104 | u32 | page_send_stage |
| 108 | u32 | page_len |
| 112 | u32 | page_fd_type |
| 116 | u32 | page_sigpipe |
| 120 | u32 | stamp_len |
| 124 | u32 | page_slot_bytes |

## 5. Stamp/page tuple and modes

Kotlin computes both buffers from the same tuple:
- page tuple at its waiter slot: `pc = (target - 8) & ~3`, `value`, `fake_task`, `fake_lock`;
- stamp tuple at `stamp_waiter_offset+0x00/+0x08`: same `pc`, `value`; fake_task/fake_lock at their offsets.
- `mode=1 (Zero)` requires `value=0`; page/stamp tuple uses `target-8 & ~3`, value 0; `preserve_child` may be 0/1.
- `mode=2 (Credential)` requires `value!=0`; page/stamp tuple uses `target-8 & ~3`, value; `preserve_child` may be 0/1.
- There is no Preserve mode; `preserve_child` is an independent flag, matching `WriteMode` (Zero=1, Credential=2).
- `page_waiter_offset/task_offset/lock_offset` are the only page-slot locators; `page_slot_size` bounds every page read/write. The old `offset` field is removed (reserved 0), because legacy `execute()` ignores it.
- Equality algorithm: read u64 at `page+page_waiter_offset` and `+8`; read u64 at `stamp+stamp_waiter_offset` and `+8`; require equal `pc/value`; read fake_task/fake_lock at their page/stamp slots and require equal. Reject if any read crosses `page_slot_size`/`stamp_len`.
- C++ rejects before socket only on independent bounds: `page_len <= page buffer`, `stamp_len <= max_stamp`, page/stamp tuple slots (16 bytes) within their buffers, `page_slot_size >= 16`; it does **not** require `page_len == stamp_len` (normal page_len=0x10000, stamp_len smaller must pass).
- Reject on page/stamp tuple mismatch, mode/value mismatch, or slot out of bounds. No write on mismatch.
- Host byte tests pin the tuple for W1/W2/W3 and the mismatch paths.

## 6. Ownership / fd

- socket fd is opened and closed inside waiter trigger; it never crosses JNI.
- pageFd belongs to prepare; route borrows it only for the page send and never closes it.
- Kotlin closes prepare fds after route returns.
- actor buffers are owned by C++ caller frame; no allocation in PI.
- exact page write required; short/negative page write => no actors. Current `native_attempt_43499.cpp:219-227` already returns on `page_written < 0` and on `page_written != page_length`; the route must reuse this guard, not a post-actor count.
- Route page send is a new `send_page_bounded(fd, page, len, timeout_ms)`: `getsockopt(SO_TYPE)` must report a socket (else `page_send_stage=3`), `poll(POLLOUT)` with deadline, then `send(MSG_DONTWAIT|MSG_NOSIGNAL)`, exact-length check. Timeout/partial/peer-close sets `page_send_stage=2/4/5`, `page_timeout` as applicable, `page_stage=0`, `actors_started=0`. The blocking `native_attempt` write is NOT used.
- `page_send_stage` values: 0 none, 1 poll+send ok, 2 timeout, 3 non-socket, 4 peer/errno, 5 short. `page_sigpipe=1` records SIGPIPE suppression via MSG_NOSIGNAL; no process signal.

## 7. PI safety

- setattr runs only in consumer actor. Legacy anchor puts it inside `PI-IN-CONSUMER`; therefore the plan does **not** claim it is outside PI. Implementation requires the explicit exception/fallback decision in §7a.
- trigger itself has no allocation/print/indirect dispatch; all pointers concrete.
- ghost disarm is the only post-park LOCK_PI_until and runs in waiter thread.
- AVD gate must include dmesg/no panic, cold boot, fixed CPUs, single route.

## 7a. PI exception (user-approved)

- Legacy consumer `Ops::setattr` is inside its `PI-IN-CONSUMER` marker (`multicast_route.hpp:758-795`); `pi-window-constraints.md:60-64` names that path as the measured panic cause.
- **User approval 2026-10-20:** PI panic is accepted as a probabilistic, non-destructive event whose consequence is a reboot; the user explicitly approved entering this route.
- Record this as an informed exception; keep the cold-boot/dmesg gate and report panic/reboot in the device gate. This exception does not lower the N3/N5 evidence bar.

## 8. Gate / rollback

N3 order gate:
- pageBytes == page.length; `page_timeout=0`; `page_send_stage=1`; `page_stage=1` only after exact write; `page_fd_type` is socket; `page_sigpipe` recorded; actor count is 0 on any page failure;
- `stamp_equal=1` and `page_slot_equal=1`; `page_len`/`stamp_len`/`page_slot_bytes` bounded independently; stamp_result/stamp_errno recorded;
- consumer calls>=1 and successes>=1;
- requeue result recorded (legacy success may be EDEADLK; judge by counters);
- `route_done=1`, `join_timeout=0`; `disarm_result/disarm_errno` recorded in the fixed order;
- `socket_closed=1`; no fd leak; dmesg no panic.

N5 gate:
- victim uid=0; only this marks landing.

Rollback:
- `GL_ENABLE_ROUTE_PORT` CMake option default OFF; port not injected => ENOSYS; existing NativeAttempt/prepare unchanged.
- failure => stop batch; legacy C++ route untouched and fail-closed; no route switch in production.
- no UI/config wiring.
