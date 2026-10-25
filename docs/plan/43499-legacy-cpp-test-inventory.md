# 43499 legacy 测试承接 inventory

- 从属于 [实施指南](43499-legacy-cpp-jni-migration-guide.md)，A0 必须把本表的“目标断言”细化为 `new path:testName`。
- 删除门：每个旧测试文件都必须有 old assertion/contract→new test 的逐项映射；fixture 只能随最后一个消费者退役。

| legacy 测试文件 | 目标层 | 目标断言/处置 |
|---|---|---|
| `tests/address_space_test.cpp` | Kotlin | final address/domain/overflow fixed vectors |
| `tests/backend_contract_test.cpp` | Kotlin/schema | backend identity 与 manifest 对拍 |
| `tests/component_catalog_test.cpp` | Kotlin/schema | support matrix；C++ catalog 退役 |
| `tests/cpp_link_probe.cpp` | C ABI | generated prototype/link firewall |
| `tests/cpp_link_probe.h` | C ABI fixture | 随 link probe 新 fixture 退役 |
| `tests/cpp_link_probe_test.c` | C ABI | C/JNI manifest signature/link test |
| `tests/futex_hash_test.cpp` | common host | hash fixed vectors |
| `tests/handoff_probe_test.cpp` | Kotlin Finish/Native | marker checks 进 Kotlin；same-fd/transfer 留 Native |
| `tests/heap_context_test.cpp` | 43499 C11 | Page partial failure/borrow/quarantine |
| `tests/kernelsnitch_scan_bounds_test.cpp` | common+Kotlin | 0/1/MAX/overflow；策略 default 在 Kotlin |
| `tests/native_resource_test.cpp` | per-DSO registry | fd/mmap/thread/child cleanup、double close |
| `tests/number_parse_test.cpp` | Kotlin | typed input/parser；Native 文本 parser 退役 |
| `tests/payload_builder_test.cpp` | Kotlin | payload bytes/metadata fixed vectors |
| `tests/pi_race_test.cpp` | 43499 C11 | actor partial-start/cancel/join/dirty closure |
| `tests/profile_binary_test.cpp` | Kotlin | schema/JCS/profile binary compatibility |
| `tests/profile_test.cpp` | Kotlin | profile model/accessor 与唯一迁移点 |
| `tests/route_catalog_test.cpp` | Kotlin/schema | mechanism support inventory |
| `tests/route_controller_test.cpp` | Kotlin wrapper | mechanism 选择只在 Kotlin |
| `tests/route_lifecycle_test.cpp` | 43499 C11 | prepare/execute/disarm/cleanup 每退出路径 |
| `tests/route_policy_test.cpp` | Kotlin Flow/Lua | supported/fallback/retry/forge credential hook |
| `tests/route_status_test.cpp` | ABI+Kotlin | status/errno/effect layout 与解释 |
| `tests/run_state_test.cpp` | Kotlin Flow | progress/state；stdin ACK 退役 |
| `tests/runtime_paths_test.cpp` | Kotlin Finish | path 生成/清理；Native only typed path |
| `tests/runtime_time_test.cpp` | consumer/general | monotonic/deadline，无业务 default |
| `tests/select_stack_route_test.cpp` | 43499 C11 | select mechanism owner/effect/direct entry |
| `tests/target_constants_test.cpp` | Kotlin | checked arithmetic/geometry fixed vectors |
| `tests/tcp_zerocopy_route_test.cpp` | 43499 C11 | TCP mechanism owner/effect/direct entry |
| `tests/victim_context_test.cpp` | 43499 C11 | pid/pgid/pipe/kill/wait/transfer/stale |
| `tests/host/attack_stub.cpp` | fixture | 随最后一个旧 pipeline 测试退役 |
| `tests/host/backend_dataflow_test.cpp` | Kotlin+JNI | typed request→registry→result reachability |
| `tests/host/glue_stub.cpp` | fixture | 用 generated JNI seam fixture 取代 |
| `tests/host/handoff_stub.cpp` | fixture | 用 Victim→RootedVictim transfer fixture 取代 |
| `tests/host/host_attack_script.cpp` | fixture | UI Finish script fixture；不含生产策略 |
| `tests/host/host_attack_script.hpp` | fixture | 随新 Finish fixture 退役 |
| `tests/host/include/sched.h` | fixture | 随仍需 host syscall shim 的目标测试保留 |
| `tests/host/include/attack/ops.hpp` | fixture | 旧 attack stub 退役时删除，不进入新 C11 include |
| `tests/host/include/linux/futex.h` | fixture | 随 common/actor host syscall shim 的最后消费者保留 |
| `tests/host/include/linux/memfd.h` | fixture | 随 page/victim host syscall shim 的最后消费者保留 |
| `tests/host/include/sys/prctl.h` | fixture | 随 process helper host syscall shim 的最后消费者保留 |
| `tests/host/route_stub.cpp` | fixture | 三 named mechanism seam 取代 |
| `tests/host/support_stub.cpp` | fixture | per-owner C11 fixture 取代 |
| `tests/host/victim_stub.cpp` | fixture | protocol victim/transfer fixture 取代 |

新增而旧树无直接对应的强制测试：

1. common/43499 两个 registry 的 DSO tag 隔离与 cross-DSO `WRONG_KIND/STALE_HANDLE`。
2. unload 的 clean/dirty/unknown/inFlight/borrowed/容量边界白盒分支。
3. atfork prepare/parent/child、epoch stale、fork-while-execute/close、fd allowlist。
4. execute×execute/cancel/close、close×close、prepareActor×destroyPage。
5. handoff 成功 consume、旧 Victim stale、失败线性化点两侧清理、handoff×cancel/close。
6. Lua 无 scope 创建 owner 失败、run 后 userdata stale、不能通过受支持 API取得 raw ID。
