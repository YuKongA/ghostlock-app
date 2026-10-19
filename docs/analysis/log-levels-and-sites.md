# 日志级别与全部日志调用点（2026-10-17）

## 1 架构（参照上游）

- 上游 5f184760 的日志形状：阶段行 + 区域汇总，每块不打印；例如 write result=ok i=… blocks=… bytes=…，finish … written=… verified=…。
- 本仓沿用同一形状：每块/每帧/每次 helper 的数字降到 V，阶段汇总降到 D，人类可读的里程碑留 I。
- 默认阈值 Info：core/src/support/gl_log.hpp 的 level_slot（native）与 TypedBackendProbe.minLogLevel（Kotlin）。
  打开 V/D 的开关按你的要求以后做——两处各留了一个入口（set_min_level / minLogLevel）。
- 文件日志第二列现在就是级别字符：HH:MM:SS.mmm <V|D|I> module:line body。

## 2 native 调用点

计数：V=73 · D=45 · I=40（共 158 处；不含 gl_log.hpp 的宏定义）

| 位置 | 级别 | 调用 |
|---|---|---|
| core/src/device/43499/chain.cpp:35 | I | GL_LOGI("43499: make_access route=%d", route); |
| core/src/device/43499/chain.cpp:139 | I | GL_LOGI("43499: initialize access=%llu route=%llu", static_cast<unsigned long long>(accessId), static_cast<unsigned long |
| core/src/device/43499/chain.cpp:157 | I | GL_LOGI("43499: write access=%llu route=%llu bytes=%zu", static_cast<unsigned long long>(accessId), static_cast<unsigned |
| core/src/device/43499/chain.cpp:193 | I | GL_LOGI("43499: read access=%llu route=%llu bytes=%zu", static_cast<unsigned long long>(accessId), static_cast<unsigned  |
| core/src/device/43284/chain.cpp:361 | D | GL_LOGD("wait_for_module -> %s", reason == nullptr ? "(success)" : reason); |
| core/src/device/43284/chain.cpp:418 | V | GL_LOGV("write_block enter target=%s session=%d up=%d bytes=%zu offset=%llu", target, |
| core/src/device/43284/chain.cpp:426 | V | GL_LOGV("write_block face: token=0x%llx face=%zu found=%d door_slot=%zu file_fd=%d remote=%d offset=%llu len=%zu", |
| core/src/device/43284/chain.cpp:432 | D | GL_LOGD("write_block -> false no-landing=%d bytes=%zu", landing == nullptr ? 1 : 0, |
| core/src/device/43284/chain.cpp:438 | D | GL_LOGD("write_block -> false slot=%zu fd=%d token=0x%llx", slot, |
| core/src/device/43284/chain.cpp:445 | I | GL_LOGI("write_block -> false land_block refused offset=%llu face=%zu", |
| core/src/device/43284/chain.cpp:451 | D | GL_LOGD("write_block -> true sequence=%llu offset=%llu face=%zu", |
| core/src/device/43284/chain.cpp:464 | V | GL_LOGV("read_block face: token=0x%llx face=%zu found=%d door_slot=%zu file_fd=%d remote=%d offset=%llu len=%zu", |
| core/src/device/43284/chain.cpp:473 | D | GL_LOGD("read_block -> false slot=%zu fd=%d remote_door=%d token=0x%llx", slot, |
| core/src/device/43284/chain.cpp:481 | D | GL_LOGD("read_block -> false no-landing offset=%llu", static_cast<unsigned long long>(request.offset)); |
| core/src/device/43284/chain.cpp:487 | V | GL_LOGV("read via landing: offset=%llu ok=%d head=%02x%02x%02x%02x", |
| core/src/device/43284/chain.cpp:546 | D | GL_LOGD("resolve: %s exists but this process cannot open it (errno=%d) -> remote", |
| core/src/device/43284/chain.cpp:619 | D | GL_LOGD("esp: socket fd=%d sender_port=%u encap_port=%u", fd, |
| core/src/device/43284/chain.cpp:638 | D | GL_LOGD("esp: socket local_port=%u (the port the SA must be told)", |
| core/src/device/43284/chain.cpp:642 | D | GL_LOGD("esp: bind(local) ok, connecting to peer port=%u", |
| core/src/device/43284/chain.cpp:652 | D | GL_LOGD("esp: ready local_port=%u peer_port=%u (both loopback)", |
| core/src/device/43284/chain.cpp:798 | D | GL_LOGD("make_surface index=%zu targetId=0x%llx target_slot=%zu fd=%d path=%s socket=%d", |
| core/src/device/43284/chain.cpp:811 | D | GL_LOGD("make_surface fd=%d points at %s", g_doors[target_slot].fd, |
| core/src/device/43284/chain.cpp:829 | I | GL_LOGI("surface: bridge refused helper=%s carrier=%s", helper_path, carrier.c_str()); |
| core/src/device/43284/chain.cpp:834 | V | GL_LOGV("surface: bridge bound helper=%s carrier=%s (len=%zu)", |
| core/src/device/43284/chain.cpp:842 | V | GL_LOGV("surface: slot=%zu carrier is remote (%s) -> the bridge reads and splices it", |
| core/src/device/43284/chain.cpp:913 | D | GL_LOGD("landing_of_door hit token=0x%llx surf=%zu door_path=%s", |
| core/src/device/43284/chain.cpp:919 | D | GL_LOGD("landing_of_door ambiguous token=0x%llx want=%zu n=%zu", |
| core/src/device/43284/chain.cpp:922 | D | GL_LOGD("landing_of_door miss token=0x%llx want=%zu surfaces=%zu", |
| core/src/device/43284/chain.cpp:963 | D | GL_LOGD("landing: built slot=%zu (window over this surface)", slot); |
| core/src/device/43284/chain.cpp:999 | D | GL_LOGD("landing: bring_up slot=%zu up=%d sa_set=%d spi=0x%08x encap=%u sender=%u icv=%u", |
| core/src/device/43284/chain.cpp:1043 | V | GL_LOGV("land_block -> false landing=%llu slot=%zu", |
| core/src/device/43284/chain.cpp:1048 | V | GL_LOGV("land_block enter: landing=%llu slot=%zu offset=%llu len=16 remote=%d file_fd=%d", |
| core/src/device/43284/chain.cpp:1055 | V | GL_LOGV("land_block -> %d offset=%llu slot=%zu", landed ? 1 : 0, |
| core/src/device/43284/chain.cpp:1072 | D | GL_LOGD("read_landing_block -> false landing=%llu slot=%zu", |
| core/src/device/43284/chain.cpp:1077 | V | GL_LOGV("read_landing_block enter: landing=%llu slot=%zu offset=%llu len=16 file_fd=%d", |
| core/src/device/43284/chain.cpp:1083 | D | GL_LOGD("read_landing_block -> %d offset=%llu slot=%zu head=%02x%02x%02x%02x", got ? 1 : 0, |
| core/src/device/43284/chain.cpp:1113 | D | GL_LOGD("access: created index=%zu landingSlot=%zu", index, slot); |
| core/src/device/43284/chain.cpp:1144 | D | GL_LOGD("initialize received: seq=%lld present=%d spi=0x%08x encap=%u sender=%u", |
| core/src/device/43284/chain.cpp:1189 | D | GL_LOGD("initialize: session landing up=%d", session_up ? 1 : 0); |
| core/src/device/43284/chain.cpp:1307 | D | GL_LOGD("write_image enter: accessId=%llu offset=%llu width=%zu bytes=%zu budget_ms=%llu", |
| core/src/device/43284/chain.cpp:1312 | D | GL_LOGD("write_image params: carrier=%s patch=%s dev=%s sa_set=%d spi=0x%08x encap=%u sender=%u icv=%u", |
| core/src/device/43284/chain.cpp:1325 | V | GL_LOGV("write_image face: access_slot=%zu landing_slot=%zu remote=%d door_slot=%zu file_fd=%d", |
| core/src/device/43284/chain.cpp:1343 | I | GL_LOGI("write_image verified=%d refusal=%s", wrote ? 1 : 0, refusal != nullptr ? refusal : "(null)"); |
| core/src/device/43284/chain.cpp:1345 | I | GL_LOGI("%swrite outcome: written=%zu verified=%zu rolled=%zu overflow=%d incomplete=%d", |
| core/src/device/43284/chain.cpp:1361 | D | GL_LOGD("write benchmark: blocks=%zu elapsed_ms=%lld per_block_us=%lld", |
| core/src/device/43284/chain.cpp:1372 | D | GL_LOGD("write_image land: landing_slot=%zu splice_error=%d (0=dispatched, 20/21/22/23=splice stage)", |
| core/src/device/43284/chain.cpp:1389 | D | GL_LOGD("write_image -> %d why=%s", wrote ? 1 : 0, |
| core/src/device/43284/chain.cpp:1461 | V | GL_LOGV("hook write face: access=%llu slot=%zu landing_slot=%zu remote=%d door_slot=%zu file_fd=%d width=%llu", |
| core/src/device/43284/chain.cpp:1471 | D | GL_LOGD("hook: map hook_vaddr=0x%llx hook_file=0x%llx payload_vaddr=0x%llx payload_file=0x%llx", |
| core/src/device/43284/chain.cpp:1476 | D | GL_LOGD("hook: plan site_file=0x%llx site_vaddr=0x%llx guard=0x%08x displaced=0x%08x payload_file=0x%llx max=%llu", |
| core/src/device/43284/chain.cpp:1483 | I | GL_LOGI("hook: write_hook wrote=%d refusal=%s plan=%p width=%llu", hook_wrote ? 1 : 0, |
| core/src/device/43284/chain.cpp:1488 | I | GL_LOGI("write outcome: written=%zu verified=%zu rolled=%zu overflow=%d incomplete=%d (hook: no journal)", |
| core/src/device/43284/chain.cpp:1490 | I | GL_LOGI("hook write -> %d refusal=%s", hook_wrote ? 1 : 0, |
| core/src/device/43284/chain.cpp:1609 | V | GL_LOGV("restore hook face: access=%llu slot=%zu landing_slot=%zu remote=%d door_slot=%zu file_fd=%d width=%zu", |
| core/src/device/43284/chain.cpp:1619 | I | GL_LOGI("restore_hook: site_file=0x%llx guard=0x%08x displaced=0x%08x restored=%d refusal=%s", |
| core/src/backend/cve_2026_43284/hook/sentry_hook.cpp:29 | D | GL_LOGD("branch refused: unaligned from=0x%llx to=0x%llx", |
| core/src/backend/cve_2026_43284/hook/sentry_hook.cpp:35 | D | GL_LOGD("branch refused: delta not a word multiple from=0x%llx to=0x%llx delta=%lld", |
| core/src/backend/cve_2026_43284/hook/sentry_hook.cpp:42 | D | GL_LOGD("branch refused: out of range from=0x%llx to=0x%llx words=%lld", |
| core/src/backend/cve_2026_43284/hook/sentry_hook.cpp:51 | V | GL_LOGV("branch: from=0x%llx to=0x%llx delta=%lld words=%lld word=0x%08x", |
| core/src/backend/cve_2026_43284/hook/sentry_hook.cpp:96 | D | GL_LOGD("plan: site_file=0x%llx site_vaddr=0x%llx guard=0x%08x guard_skipped=%d displaced=0x%08x", |
| core/src/backend/cve_2026_43284/hook/sentry_hook.cpp:101 | D | GL_LOGD("plan: payload_file=0x%llx payload_vaddr=0x%llx max=%llu entry=0x%zx displaced_off=0x%zx jump_back_off=0x%zx", |
| core/src/backend/cve_2026_43284/hook/sentry_hook.cpp:117 | D | GL_LOGD("hook_branch refused: site=0x%llx entry=0x%llx", |
| core/src/backend/cve_2026_43284/hook/sentry_hook.cpp:123 | V | GL_LOGV("hook_branch: site=0x%llx entry=0x%llx word=0x%08x", |
| core/src/backend/cve_2026_43284/hook/sentry_hook.cpp:174 | D | GL_LOGD("payload: blob=%zu padded=%zu capacity=%zu max=%llu entry=0x%zx displaced=0x%zx jump_back=0x%zx", |
| core/src/backend/cve_2026_43284/hook/sentry_hook.cpp:177 | V | GL_LOGV("payload: displaced slot off=0x%zx len=4 word=0x%08x ascii=%s", |
| core/src/backend/cve_2026_43284/hook/sentry_hook.cpp:179 | V | GL_LOGV("payload: jump_back slot off=0x%zx len=4 word=0x%08x ascii=%s", |
| core/src/backend/cve_2026_43284/kernel_access.hpp:78 | I | GL_LOGI("initialize: adopt seq=%u present=%d window=%p", params->sequence, |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:97 | V | GL_LOGV("hook: read site block at=%llu width=%zu channel=%d offset_set=%d", |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:113 | D | GL_LOGD("hook: site bytes %s", site_hex); |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:141 | V | GL_LOGV("hook slot: entry off=0x%zx len=4 payload_file=0x%llx", |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:144 | V | GL_LOGV("hook slot: displaced off=0x%zx len=4 word=0x%08x ascii=%s", |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:146 | V | GL_LOGV("hook slot: jump_back off=0x%zx len=4 vaddr=0x%llx", |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:149 | V | GL_LOGV("hook site: off=0x%llx within=%zu len=%zu ascii=%s", |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:151 | D | GL_LOGD("hook site before: guard=0x%08x displaced=0x%08x branch=0x%08x ascii=%s", |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:174 | V | GL_LOGV("hook payload: off=0x%llx size=%zu width=%zu max=%llu entry=0x%zx", |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:209 | D | GL_LOGD("hook site writeback: wrote=%zu/%zu off=0x%llx branch=0x%08x readback_ok=%d readback_same=%d", |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:254 | D | GL_LOGD("restore: site block off=0x%llx within=%zu guard=0x%08x displaced=0x%08x", |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:290 | V | GL_LOGV("restore writeback: off=0x%llx width=%zu attempts=%zu readback_same=%d", |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:430 | V | GL_LOGV("rollback block[%zu]: off=%llu restored=%d rolled=%zu incomplete=%d", |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:435 | D | GL_LOGD("rollback: from=%zu to=0 rolled=%zu incomplete=%d", |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:475 | I | GL_LOGI("journal: n=%zu overflow=%d", journal.count, journal.overflow ? 1 : 0); |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:498 | V | GL_LOGV("image block[%zu/%zu]: off=%llu len=%zu take=%zu", |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:533 | V | GL_LOGV("block[%zu]: wrote=%zu/%zu offset=%llu budget_ms=%llu", at, written.value, request.width, |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:542 | V | GL_LOGV("block[%zu]: written, not read back (verify_every=%zu)", at, request.verify_every); |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:562 | V | GL_LOGV("block[%zu] try=%zu resent=%zu/%zu", at, tries, again.value, request.width); |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:572 | V | GL_LOGV("image block[%zu] readback: off=%llu len=%zu ok=%d size=%zu same=%d head=%02x%02x%02x%02x", |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:590 | V | GL_LOGV("block[%zu]: landed tries=%zu took=%lld us", at, tries, |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:601 | I | GL_LOGI("block[%zu] verify failed: want %02x%02x%02x%02x got %02x%02x%02x%02x", at, |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:604 | I | GL_LOGI("block[%zu] verify failed: want %02x%02x%02x%02x got unreadable (ok=%d size=%zu)", at, |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:608 | I | GL_LOGI("verify failed: block_at=%zu offset=%llu tries=%zu", at, |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:619 | D | GL_LOGD("verify scan: off=%llu head=%02x%02x", |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:625 | V | GL_LOGV("block[%zu]: budget exhausted after %zu tries", at, tries); |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:632 | I | GL_LOGI("journal: n=%zu overflow=%d", journal.count, journal.overflow ? 1 : 0); |
| core/src/backend/cve_2026_43284/steps/pagecache_step_runner.cpp:94 | I | GL_LOGI("journal: n=%zu overflow=%d rolled=%zu incomplete=%d", written.entries, |
| core/src/backend/cve_2026_43284/steps/pagecache_step_runner.cpp:141 | I | GL_LOGI("runner: door=%s target_fd=%d bridge=%d encap_port=%u sa_set=%d width=%zu", |
| core/src/backend/cve_2026_43284/steps/pagecache_step_runner.cpp:195 | I | GL_LOGI("patch1: image=%zu offset=%llu width=%zu blocks=%zu journal_bytes=%zu source_fd=%d bridge=%d", |
| core/src/backend/cve_2026_43284/steps/pagecache_step_runner.cpp:259 | I | GL_LOGI("runner: step=%s outcome=%s refusal=%s splice_error=%d", |
| core/src/backend/cve_2026_43284/landing/pagecache_write.hpp:118 | I | GL_LOGI("window: refused bridge-mode mismatch doors=%d surface=%d", |
| core/src/backend/cve_2026_43284/landing/pagecache_write.hpp:135 | V | GL_LOGV("window: doors open spi=0x%08x icv_len=%u next_seq=%u file_fd=%d socket_fd=%d bridge_mode=%d", |
| core/src/backend/cve_2026_43284/landing/pagecache_write.hpp:169 | V | GL_LOGV("frame: spi=0x%08x seq=%u icv_len=%u iv=%02x%02x%02x%02x old=%02x%02x%02x%02x want=%02x%02x%02x%02x", |
| core/src/backend/cve_2026_43284/landing/pagecache_write.hpp:177 | V | GL_LOGV("frame: segments hdr=%zu vector=%zu cipher=%zu icv=%zu total=%zu file_fd=%d socket=%d helper=%d", |
| core/src/backend/cve_2026_43284/landing/pagecache_write.hpp:195 | V | GL_LOGV("write_block enter pos=%llu fd=%d sock=%d serve=%d", |
| core/src/backend/cve_2026_43284/landing/pagecache_write.hpp:198 | V | GL_LOGV("window: send seq=%u offset=%llu hdr=%02x%02x%02x%02x%02x%02x%02x%02x", next_seq_, |
| core/src/backend/cve_2026_43284/landing/pagecache_write.hpp:215 | V | GL_LOGV("window: splice NOT sent offset=%llu err=%d seq=%u (20=hdr 21=page 22=len 23=suffix)", |
| core/src/backend/cve_2026_43284/landing/pagecache_write.hpp:217 | V | GL_LOGV("frame not sent: next_seq stays=%u file_fd=%d", next_seq_, doors_.file_fd()); |
| core/src/backend/cve_2026_43284/landing/pagecache_write.hpp:225 | V | GL_LOGV("frame sent: offset=%llu bytes=%zu seq_used=%u next_seq=%u socket=%d file_fd=%d", |
| core/src/backend/cve_2026_43284/landing/pagecache_write.hpp:243 | V | GL_LOGV("read_block: not open can_read=%d fd=%d pos=%llu", |
| core/src/backend/cve_2026_43284/landing/pagecache_write.hpp:256 | V | GL_LOGV("read_block: fd=%d pos=%llu pread=%ld direct=%02x%02x%02x%02x", |
| core/src/backend/cve_2026_43284/landing/pagecache_write.hpp:263 | V | GL_LOGV("read_block: fd=%d flags=0x%x link=%s", doors_.file_fd(), |
| core/src/backend/cve_2026_43284/landing/pagecache_write.hpp:268 | V | GL_LOGV("read_block: incomplete fd=%d pos=%llu len=%zu", |
| core/src/backend/cve_2026_43284/landing/pagecache_write.hpp:277 | V | GL_LOGV("read_block: ok fd=%d pos=%llu len=%zu head=%02x%02x%02x%02x", |
| core/src/backend/cve_2026_43284/landing/pagecache_write.hpp:295 | V | GL_LOGV("window: adopt_sequence from=%u to=%u", next_seq_, next == 0U ? 1U : next); |
| core/src/backend/cve_2026_43284/landing/real_splice_io.cpp:199 | V | GL_LOGV("splice read_at: fd=%d len=%zu offset=%llu", fd, size, |
| core/src/backend/cve_2026_43284/landing/real_splice_io.cpp:231 | V | GL_LOGV("splice read_at: direct ok fd=%d len=%zu offset=%llu", fd, size, |
| core/src/backend/cve_2026_43284/landing/real_splice_io.cpp:245 | V | GL_LOGV("splice read_at: bridge fd=%d offset=%llu (direct code=%ld)", |
| core/src/backend/cve_2026_43284/landing/real_splice_io.cpp:249 | I | GL_LOGI("splice read_at: direct failed fd=%d len=%zu offset=%llu code=%ld", fd, size, |
| core/src/backend/cve_2026_43284/landing/real_splice_io.cpp:257 | I | GL_LOGI("tee: refused off=0x%llx pipe2 failed", static_cast<unsigned long long>(offset)); |
| core/src/backend/cve_2026_43284/landing/real_splice_io.cpp:284 | V | GL_LOGV("tee: block off=0x%llx page_pipe=%d", |
| core/src/backend/cve_2026_43284/landing/real_splice_io.cpp:289 | I | GL_LOGI("tee: refused off=0x%llx pipe2 failed", static_cast<unsigned long long>(offset)); |
| core/src/backend/cve_2026_43284/landing/real_splice_io.cpp:308 | I | GL_LOGI("tee: block off=0x%llx page_pipe=%d tee=%ld got=%ld failed=1", |
| core/src/backend/cve_2026_43284/landing/real_splice_io.cpp:313 | V | GL_LOGV("tee: preimage off=0x%llx head=%02x%02x%02x%02x", |
| core/src/backend/cve_2026_43284/landing/real_splice_io.cpp:377 | I | GL_LOGI("tee: refused pending=0x%llx send=0x%llx", |
| core/src/backend/cve_2026_43284/landing/real_splice_io.cpp:397 | V | GL_LOGV("splice_one: socket=%d size=%zu offset=%llu before_page=%zu page=%zu file_fd=%d helper=%d", |
| core/src/backend/cve_2026_43284/landing/real_splice_io.cpp:405 | V | GL_LOGV("splice_one: pipe=[%d,%d] flags=0x%x", fds[0], fds[1], static_cast<unsigned>(pipe_flags_)); |
| core/src/backend/cve_2026_43284/landing/real_splice_io.cpp:408 | V | GL_LOGV("splice_one: segment hdr+vector=%zu -> pipe=%d", kBeforePageBytes, fds[1]); |
| core/src/backend/cve_2026_43284/landing/real_splice_io.cpp:412 | V | GL_LOGV("splice_one: segment page in_fd=%d -> pipe=%d len=%zu at=%llu", |
| core/src/backend/cve_2026_43284/landing/real_splice_io.cpp:415 | V | GL_LOGV("splice_one: page spliced=%llu want=%zu cursor=%llu", |
| core/src/backend/cve_2026_43284/landing/real_splice_io.cpp:421 | V | GL_LOGV("tee: spliced=%ld", spliced); |
| core/src/backend/cve_2026_43284/landing/real_splice_io.cpp:440 | V | GL_LOGV("helper splice: offset=%llu spliced=%ld probed=%ld head=%02x%02x%02x%02x fd=%d helper_avail=%d", |
| core/src/backend/cve_2026_43284/landing/real_splice_io.cpp:446 | V | GL_LOGV("helper splice: offset=%llu spliced=%ld (probe off-cadence)", |
| core/src/backend/cve_2026_43284/landing/real_splice_io.cpp:452 | V | GL_LOGV("splice_one: segment icv=%zu (prefix=%zu)", suffix, kMinDatagramBytes); |
| core/src/backend/cve_2026_43284/landing/real_splice_io.cpp:455 | V | GL_LOGV("splice_one: datagram sent=%ld want=%zu pipe=%d socket=%d", |
| core/src/backend/cve_2026_43284/landing/real_splice_io.cpp:463 | V | GL_LOGV("splice_one: done ok=%d last_error=%d (0=no failure, 10=pipe, 20=hdr, 21=page, 22=helper, 23=icv, 24=short)", |
| core/src/backend/cve_2026_43284/landing/pagecache_landing.hpp:94 | I | GL_LOGI("landing: bring_up refused already_up=%d sa_set=%d", up_ ? 1 : 0, sa.set ? 1 : 0); |
| core/src/backend/cve_2026_43284/landing/pagecache_landing.hpp:98 | I | GL_LOGI("landing: bring_up refused at open_doors"); |
| core/src/backend/cve_2026_43284/landing/crash_dump_bridge.cpp:133 | D | GL_LOGD("bridge: bound helper=%s carrier=%s mode=session", helper_, carrier_); |
| core/src/backend/cve_2026_43284/landing/crash_dump_bridge.cpp:142 | D | GL_LOGD("bridge: session start=%d pid=%d mode=%d", session_started_ ? 1 : 0, |
| core/src/backend/cve_2026_43284/landing/crash_dump_bridge.cpp:159 | I | GL_LOGI("bridge: session read refused offset=%llu outcome=%d requests=%zu", |
| core/src/backend/cve_2026_43284/landing/crash_dump_bridge.cpp:179 | I | GL_LOGI("bridge: session splice refused offset=%llu outcome=%d requests=%zu", |
| core/src/backend/cve_2026_43284/landing/crash_dump_bridge.cpp:202 | V | GL_LOGV("bridge: read16 offset=%llu len=%zu out=%d ctx=%d", |
| core/src/backend/cve_2026_43284/landing/crash_dump_bridge.cpp:220 | V | GL_LOGV("bridge: splice16 offset=%llu len=%zu pipe_write_fd=%d ctx=%d", |
| core/src/backend/cve_2026_43284/landing/crash_dump_bridge.cpp:246 | V | GL_LOGV("bridge: run offset=%llu sink_stdout=%d out=%d pipe=[%d,%d] helper=%s carrier=%s", |
| core/src/backend/cve_2026_43284/landing/crash_dump_bridge.cpp:275 | I | GL_LOGI("bridge: spawn refused offset=%llu helper=%s", static_cast<unsigned long long>(offset), |
| core/src/backend/cve_2026_43284/landing/crash_dump_bridge.cpp:279 | V | GL_LOGV("bridge: spawn pid=%ld read_fd=%d write_fd=%d", static_cast<long>(child), fds[0], |
| core/src/backend/cve_2026_43284/landing/crash_dump_bridge.cpp:303 | V | GL_LOGV("bridge: read got=%ld want=%zu out=%d head=%02x%02x%02x%02x", |
| core/src/backend/cve_2026_43284/landing/crash_dump_bridge.cpp:330 | V | GL_LOGV("bridge: helper status=0x%x exited=%d code=%d spawn_us=%lld wait_us=%lld", |
| core/src/backend/cve_2026_43284/landing/crash_dump_bridge.cpp:341 | I | GL_LOGI("bridge: refused helper-failed status=0x%x offset=%llu", |
| core/src/backend/cve_2026_43284/landing/crash_dump_bridge.cpp:347 | I | GL_LOGI("bridge: refused short-read got=%ld want=%zu offset=%llu", |
| core/src/backend/cve_2026_43284/landing/crash_dump_bridge.cpp:353 | V | GL_LOGV("bridge: ok offset=%llu bytes=%zu sink_stdout=%d", |
| core/src/backend/cve_2026_43284/landing/bridge_session.cpp:159 | I | GL_LOGI("bridge: session refused socketpair errno=%d", errno); |
| core/src/backend/cve_2026_43284/landing/bridge_session.cpp:167 | I | GL_LOGI("bridge: session refused pipe errno=%d", errno); |
| core/src/backend/cve_2026_43284/landing/bridge_session.cpp:184 | I | GL_LOGI("bridge: session refused fork errno=%d", errno); |
| core/src/backend/cve_2026_43284/landing/bridge_session.cpp:213 | V | GL_LOGV("bridge: opened pid=%d mode=session", static_cast<int>(child)); |
| core/src/backend/cve_2026_43284/landing/bridge_session.cpp:215 | I | GL_LOGI("bridge: refused session pid=%d outcome=%d (a helper that is not a session helper must not be believed)", |
| core/src/backend/cve_2026_43284/landing/bridge_session.cpp:222 | V | GL_LOGV("bridge: session ready pid=%d helper=%s carrier=%s", |
| core/src/backend/cve_2026_43284/landing/bridge_session.cpp:284 | V | GL_LOGV("bridge: closed pid=%d wait=%d fds=%d", static_cast<int>(pid), wait, closed); |
| core/src/backend/cve_2026_43284/landing/bridge_session.cpp:430 | V | GL_LOGV("bridge: request op=%c off=0x%llx us=%lld", letter, |
| core/src/backend/cve_2026_43284/landing/bridge_session.cpp:434 | I | GL_LOGI("bridge: request failed op=%c off=0x%llx outcome=%d requests=%zu failed=%zu", letter, |

## 3 Kotlin 调用点（step / dstep / vstep）

计数：V=21 · D=12 · I=111（共 144 处）

| 位置 | 级别 | 调用 |
|---|---|---|
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:229 | I | step("typed: ipsec spi skipped no-ipsec-service") |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:235 | I | step("typed: ipsec spi allocated=0x" + Integer.toHexString(allocated.spi)) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:239 | I | step("typed: ipsec spi closed") |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:242 | I | step("typed: ipsec spi refused " + problem.javaClass.simpleName + ":" + (problem.message ?: "")) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:253 | I | step("typed: ipsec encap skipped no-ipsec-service") |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:258 | I | step("typed: ipsec encap opened port=4500") |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:262 | I | step("typed: ipsec encap closed") |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:265 | I | step("typed: ipsec encap refused " + problem.javaClass.simpleName + ":" + (problem.message ?: "")) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:306 | I | step("typed: sa frame refused " + problem.javaClass.simpleName + ":" + (problem.message ?: "")) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:338 | I | step("typed: ipsec sa refused no-ipsec-service") |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:348 | I | step("typed: ipsec sa encapPort=" + encapPort) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:357 | I | step("typed: ipsec sa senderPort=" + saSenderPort) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:365 | I | step("typed: ipsec sa spi=0x" + Integer.toHexString(spi)) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:398 | I | step("typed: ipsec sa refused " + problem.javaClass.simpleName + ":" + (problem.message ?: "")) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:419 | I | step("typed: ipsec sa frame=" + written + " (SA 已建、帧按同一材料落盘)") |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:421 | I | step("typed: ipsec sa frame refused " + problem.javaClass.simpleName + ":" + (problem.message ?: "")) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:833 | I | step("typed: 43499 library missing (lib" + Native43499.LIBRARY + ".so)") |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:836 | I | step("typed: 43499 library loaded") |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:843 | I | step("typed: 43499 create failed") |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:846 | I | step("typed: 43499 create ok") |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:855 | I | step("typed: 43499 initialize code=" + code + " refusal=" + refusal[0]) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:869 | I | step("typed: 43499 prepare code=" + code + " refusal=" + refusal[0]) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:874 | I | step("typed: 43499 writeWord code=" + code + " refusal=" + refusal[0]) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:879 | I | step("typed: 43499 finishVictim code=" + code + " refusal=" + refusal[0]) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:884 | I | step("typed: 43499 shutdown code=" + code + " refusal=" + refusal[0]) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:888 | I | step("typed: 43499 destroy code=" + code + " refusal=" + refusal[0]) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:892 | I | step("typed: 43499 shutdown-after-destroy code=" + code + " refusal=" + refusal[0]) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:932 | I | step("typed: 43284 library missing (lib" + Native43284.LIBRARY + ".so)") |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:936 | I | step("typed: 43284 library loaded") |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:943 | I | step("typed: 43284 create failed") |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:947 | I | step("typed: 43284 create ok") |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:952 | I | step("typed: 43284 initialize code=" + code + " refusal=" + refusal[0]) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:999 | I | step("typed: 43284 moduleCheck code=" + code + " refusal=" + refusal[0]) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1059 | I | step("typed: 43284 resolveTarget code=" + code + " ticket=" + ticket[0] + " refusal=" + refusal[0]) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1086 | I | step("typed: 43284 patch1-via-kotlin no-helper refusal=" + refusal[0]) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1109 | I | step("typed: 43284 patch1-via-kotlin blocks=" + written + " refusedAt=" + refusedAt) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1143 | V | vstep("typed: 43284 writeBlock code=" + code + " refusal=" + refusal[0]) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1165 | I | step("typed: 43284 resolveTarget(bad) code=" + code + " refusal=" + refusal[0]) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1183 | I | step("typed: 43284 trigger code=" + code + " refusal=" + refusal[0]) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1188 | I | step("typed: 43284 shutdown code=" + code + " refusal=" + refusal[0]) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1192 | I | step("typed: 43284 destroy code=" + code + " refusal=" + refusal[0]) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1203 | I | step("typed: assembled 43499 create failed") |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1228 | I | step("typed: assembled 43499 init=" + inited + " refusal=" + assembled43499.lastRefusal) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1233 | I | step("typed: assembled 43499 write=" + wrote + " refusal=" + assembled43499.lastRefusal) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1245 | I | step("typed: assembled 43499 shutdown=" + shut + " refusal=" + assembled43499.lastRefusal) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1250 | I | step("typed: assembled 43499 close=" + closed43499) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1500 | I | step("typed: assembled 43284 library missing") |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1553 | I | step("typed: assembled 43284 create failed") |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1561 | I | step("typed: assembled 43284 init code=" + session.lastCode) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1565 | I | step("typed: assembled 43284 open code=" + session.lastCode + " ticket=" + ticket) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1585 | I | step("typed: stepwise 43284 open target=" + stepTarget + " refusal=" + stepwise.lastRefusal) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1597 | I | step("typed: probe pattern wrote=" + wrote + " at=" + hex(atSame)) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1601 | I | step("typed: probe pattern below=" + hex(atBelow)) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1605 | I | step("typed: probe pattern above=" + hex(atAbove)) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1606 | I | }.onFailure { problem -> step("typed: probe pattern refused " + problem.javaClass.simpleName) } |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1610 | I | step("typed: stepwise 43284 socket=" + stepSocket + " refusal=" + stepwise.lastRefusal) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1614 | I | step("typed: stepwise 43284 surface=" + stepSurface + " refusal=" + stepwise.lastRefusal) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1618 | I | step("typed: stepwise 43284 landing=" + stepLanding + " refusal=" + stepwise.lastRefusal) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1620 | I | step("typed: stepwise 43284 bringUp=skipped-path-b") |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1637 | I | step("typed: stepwise 43284 access=" + stepAccess + " refusal=" + stepwise.lastRefusal) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1679 | I | step("typed: stepwise 43284 accessInit=" + accessInit + " refusal=" + stepwise.lastRefusal) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1695 | D | dstep("typed: stepwise 43284 patch1 wrote=" + patch1Wrote + " refusal=" + stepwise.lastRefusal) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1699 | I | step("typed: stepwise 43284 accessDestroy=" + stepAccessDestroyed) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1724 | I | step("typed: patcher reuses socket=" + stepwise.socketId) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1728 | I | step("typed: patcher surfaceId=" + madeSurface + " code=" + patcher.lastCode + " refusal=" + patcher.lastRefusal) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1732 | I | step("typed: patcher landingId=" + madeLanding + " code=" + patcher.lastCode + " refusal=" + patcher.lastRefusal) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1733 | I | step("typed: marker-before-ids") |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1734 | I | step("typed: patch1 door=" + patchDoor + " refusal=" + patcher.lastRefusal) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1738 | I | step("typed: patch1 access=" + patchAccess) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1749 | I | step("typed: ids refused " + problem.javaClass.simpleName + ":" + (problem.message ?: "")) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1755 | I | step("typed: patch1 init=" + patchInit + " refusal=" + patcher.lastRefusal) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1793 | I | step("typed: old-page direct=" + directHex + " viaChain=" + chainHex + |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1796 | I | step("typed: old-page refused " + problem.javaClass.simpleName + ":" + (problem.message ?: "")) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1804 | I | step("typed: next-sequence=" + patchNextSeq + " refusal=" + patcher.lastRefusal) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1806 | I | step("typed: next-sequence refused " + problem.javaClass.simpleName + ":" + (problem.message ?: "")) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1815 | I | step("typed: xfrm-stat=" + stat) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1817 | I | step("typed: xfrm-stat refused " + problem.javaClass.simpleName) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1829 | I | step("typed: encap state refused " + problem.javaClass.simpleName + ":" + (problem.message ?: "")) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1850 | I | step("typed: direct udp to encap port=" + saEncapPort + " => " + directResult) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1854 | I | step("typed: network active=" + (cm?.activeNetwork != null)) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1856 | I | step("typed: network refused " + problem.javaClass.simpleName) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1897 | V | vstep("typed: nextSequence before hook=" + seq.value + " patcherLanding=" + patcher.landingId) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1901 | I | step("typed: hook init=" + hookInit + " code=" + hook.lastCode + " refusal=" + hook.lastRefusal) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1909 | V | vstep("typed: reader wrote=" + wrote + " same=" + hex(hook.readBlock(hookDoor, 0xFE600L))) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1910 | V | vstep("typed: reader below=" + hex(hook.readBlock(hookDoor, 0xFE5F0L)) + " above=" + hex(hook.readBlock(hookDoor, 0xFE61 |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1911 | V | vstep("typed: reader skew -8=" + hex(hook.readBlock(hookDoor, 0xFE5F8L)) + " +8=" + hex(hook.readBlock(hookDoor, 0xFE608 |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1912 | V | vstep("typed: reader skew -16=" + hex(hook.readBlock(hookDoor, 0xFE5F0L)) + " +16=" + hex(hook.readBlock(hookDoor, 0xFE6 |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1913 | V | vstep("typed: reader wide -32=" + hex(hook.readBlock(hookDoor, 0xFE5E0L)) + " +32=" + hex(hook.readBlock(hookDoor, 0xFE6 |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1914 | V | vstep("typed: reader wide -64=" + hex(hook.readBlock(hookDoor, 0xFE5C0L)) + " +64=" + hex(hook.readBlock(hookDoor, 0xFE6 |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1915 | V | vstep("typed: reader wide +128=" + hex(hook.readBlock(hookDoor, 0xFE680L)) + " +256=" + hex(hook.readBlock(hookDoor, 0xF |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1916 | I | }.onFailure { problem -> step("typed: reader refused " + problem.javaClass.simpleName) } |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1973 | I | step("typed: " + checked.refusal) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1978 | I | step("typed: plan:check-refused " + problem.javaClass.simpleName + ":" + (problem.message ?: "")) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1992 | I | step("typed: hook rewrite plan site=" + site + " branch=" + brAt + " block=" + blk0 + " within=" + within + " imm=0x" +  |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:1993 | I | step("typed: hook rewrite word=0x" + word.toString(16)) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2001 | I | step("typed: hook branch rewrite=" + branchFix + " code=" + hook.lastCode + " refusal=" + hook.lastRefusal) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2008 | V | vstep("typed: hook site after=" + (blk?.joinToString(" ") { b -> "%02x".format(b) } ?: "unreadable")) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2034 | I | step("typed: hook site image wrote=" + imWrote + " word=0x" + word.toString(16) + " block=" + blk0) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2036 | I | step("typed: hook site image skipped: block unreadable") |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2038 | I | }.onFailure { problem -> step("typed: hook site image refused " + problem.javaClass.simpleName) } |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2048 | V | vstep("typed: hook adopt=" + hookAdopt2 + " seq=" + seq.value) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2057 | D | dstep("typed: hook wrote=" + hookWrote + " code=" + hook.lastCode + " refusal=" + hook.lastRefusal) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2065 | V | vstep("typed: hook payload bytes=" + (payload?.size ?: -1)) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2069 | V | vstep("typed: hook landing=" + hookLanding) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2078 | V | vstep("typed: hook payload slots=" + payloadSlots) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2087 | D | dstep("typed: hook payload wrote=" + payloadWrote + " code=" + hook.lastCode + " refusal=" + hook.lastRefusal) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2109 | I | step("typed: hook site block before=" + before.joinToString(" ") { b -> "%02x".format(b) } + " word=0x" + word2.toString |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2141 | I | step("typed: hook site block wrote=" + siteWrote + " afterWord=0x" + afterWord.toString(16) + " verified=" + (afterWord  |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2143 | I | step("typed: hook site block unreadable") |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2145 | I | }.onFailure { problem -> step("typed: hook site block refused " + problem.javaClass.simpleName) } |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2149 | I | step("typed: hook payload refused " + problem.javaClass.simpleName + ":" + (problem.message ?: "")) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2170 | D | dstep("module precheck: name=" + pre.name + " vermagic=" + pre.vermagic) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2172 | D | dstep("module precheck: refused " + pre) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2184 | V | vstep("typed: module bytes=" + moduleBytes.size) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2217 | D | dstep("typed: module init=" + moduleInit + " code=" + module.lastCode + " refusal=" + module.lastRefusal) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2243 | I | step("module write: skipped (carrier already holds this module)") |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2254 | D | dstep("typed: module wrote=" + moduleWrote + " code=" + module.lastCode + " refusal=" + module.lastRefusal) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2292 | V | vstep("typed: carrier scan off=" + probeOffset + " head=" + (blk?.take(4)?.joinToString(" ") { b -> "%02x".format(b) } ? |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2294 | V | }.onFailure { problem -> vstep("typed: carrier scan refused " + problem.javaClass.simpleName) } |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2296 | I | step("typed: module refused " + problem.javaClass.simpleName + ":" + (problem.message ?: "")) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2320 | D | dstep("typed: trigger-time apply wrote=" + triggerApplyWrote) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2322 | D | }.onFailure { problem -> dstep("typed: trigger-time apply refused " + problem.javaClass.simpleName) } |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2366 | I | //     step("typed: detonate exit=" + exit) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2368 | I | //     step("typed: detonate refused " + problem.javaClass.simpleName) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2371 | I | step("typed: detonate disabled (init-reap only, ref exec_path=nullptr)") |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2404 | I | step("typed: hook wait terminal=" + terminal + " refusal=" + hook.lastRefusal) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2445 | V | vstep("typed: retry " + attempt + " mutex=" + mutex + " slots=" + slots + " payload=" + wrote) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2491 | V | }.onFailure { problem -> vstep("typed: retry " + attempt + " refused " + problem.javaClass.simpleName) } |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2521 | I | step("typed: ksud status sysfsModule=" + (if (moduleDir) "1" else "0") + " umh=" + (if (umh) "1" else "0") + " lkmOk=" + |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2522 | I | step("typed: ksud status markers=" + markers) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2523 | I | step("typed: ksud status rootlog=" + logTail) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2525 | I | step("typed: ksud status refused " + problem.javaClass.simpleName + ":" + (problem.message ?: "")) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2534 | I | step("typed: hook refused " + problem.javaClass.simpleName + ":" + (problem.message ?: "")) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2561 | D | dstep("typed: hook restore=false refusal=" + absent) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2578 | V | vstep("typed: restore site before=" + (before?.joinToString(" ") { b -> "%02x".format(b) } ?: "null")) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2586 | V | vstep("typed: restore site after=" + (after?.joinToString(" ") { b -> "%02x".format(b) } ?: "null")) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2592 | D | dstep("typed: hook restore=" + restored + " refusal=" + restoreSession.lastRefusal) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2610 | D | dstep("typed: patch1 wrote=" + patchWrote + " refusal=" + patcher.lastRefusal + |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2615 | I | step("typed: patch1 accessDestroy=" + patchAccessDestroy) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2619 | I | step("typed: patch1 teardown=" + patchTorn) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2623 | I | step("typed: stepwise 43284 sequence=" + stepSeq) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2627 | I | step("typed: stepwise 43284 tearDown=" + stepTorn + " refusal=" + stepwise.lastRefusal) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2631 | I | step("typed: stepwise 43284 destroy=" + stepDestroyed) |
| app/src/main/kotlin/com/ghostlock/app/tempui/TypedBackendProbe.kt:2635 | I | step("typed: assembled 43284 close ok=" + closed) |

## 4 改格式只需动这四处

1. native → logcat：gl_log.hpp::write 里的 __android_log_print(INFO/DEBUG, "GhostLockNative", "%s", "file:line body")
2. native → 文件：同函数里的 "%s %c %s:%d %s\n"（时间戳、级别字符、file、line、body）
3. Kotlin → logcat：runOnce 的 emit() 里 GhostLog.i/d(TAG, line)
4. Kotlin → 文件/UI：emit() 里的 lines += line 与 lineListener?.invoke(line)

## 5 本次分类口径

- V：write outcome i=…、carrier scan、retry …、bridge: …、splice_one: …、frame: …、hook slot、readback、journal:、block[i]、seq=。
- D：write stage=region …、hook wrote=、module init=、patch1 wrote=、hook adopt=、write benchmark、resolve/plan/surface/esp、sentry_hook plan/payload。
- I：拒绝原因（refused/失败/mismatch）与链的里程碑（SA、patch、hook applied、trigger、wait outcome、restore、KernelSU）。
