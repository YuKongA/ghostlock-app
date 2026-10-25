# 43499 legacy 生产文件处置 inventory

- 从属于 [实施指南](43499-legacy-cpp-jni-migration-guide.md)，不定义第二套架构。
- 处置：R=改造复用，S=拆分，G=满足复用门槛后下沉，K=迁 Kotlin，X=退役。
- “目标”只是 symbol annex 的默认 owner；逐 symbol 审计可细化，但不得改变上位边界。

| legacy 文件 | 处置 | 目标/保留 | Kotlin/删除前置 |
|---|---|---|---|
| `main.cpp` | X | 无 CLI 总入口 | Flow/Lua 接线；四类清零 |
| `common.h` | X | 无聚合头 | 窄 include；firewall 0 |
| `attack/ops.cpp` | S/K | syscall/resource 小函数按 owner 拆；discovery 只能进 general | profile/address/script/流程进 Kotlin；逐 symbol 清零 |
| `attack/ops.hpp` | S/X | 仅对应窄 C header | 去 general/profile 依赖 |
| `kernel/constants.hpp` | S/K | mechanism 私有 UAPI 常量 | target/route/payload 常量进 Kotlin/schema |
| `kernel/offset.h` | K/X | 无 target config | final value ABI 对拍 |
| `kernel/runtime_struct_offsets.h` | K/X | 无 fallback offset | Kotlin fixed vector |
| `kernel/target.h` | S/X | 必需 UAPI 形状进 mechanism 私有头 | SoC/profile/route 进 Kotlin |
| `kernel/target_constants.hpp` | K/X | 无业务 geometry | Kotlin checked arithmetic |
| `kernelsnitch/futex_hash.h` | R | `glgeneral/kernelsnitch/futex_hash.c/.h` | host vector |
| `kernelsnitch/kernelsnitch.h` | S/R | `glgeneral/kernel_discovery.c/.h` | general registry；stop/join/munmap |
| `kernelsnitch/number_parse.h` | K/X | 无文本 parser | typed request |
| `kernelsnitch/scan_bounds.h` | S/K | general 防御 validator | 策略/bounds 进 Kotlin |
| `kernelsnitch/timeutils.h` | R/G | general 私有；双消费者后 common clock | 无业务 timeout default |
| `kernelsnitch/utils.h` | S/G/X | process helper 留 general 或 common | 删除 exit/hexdump/text/write-file 策略 |
| `memory/address_space.cpp` | K/X | 无 | Kotlin final address vectors |
| `memory/address_space.h` | K/X | 无 | 同上 |
| `memory/heap_context.cpp` | S/R | `gl43499/page_resource.c` | registry/borrow/quarantine matrix |
| `memory/heap_context.h` | S/R | `gl43499/page_resource.h` 私有 resource | generated kind/state |
| `memory/payload_builder.cpp` | K/X | Native 仅 validator | Kotlin payload vectors |
| `memory/payload_builder.h` | K/X | typed request 进 Kotlin/schema | Native include 0 |
| `profile/accessors.hpp` | K/X | 无 | Kotlin 唯一 profile |
| `profile/binary.cpp` | K/X | 无 decoder | Kotlin schema/JCS |
| `profile/binary.h` | K/X | 无 wire model | ABI 对拍 |
| `profile/entry.cpp` | K/X | 无 file/stdin entry | Kotlin loader |
| `profile/entry.h` | K/X | 无 | caller 0 |
| `profile/macros.h` | K/X | 单一 schema 生成 | 无手写第二表 |
| `profile/model.h` | K/X | 只生成 ABI POD | Kotlin 唯一 profile |
| `race/pi_race.cpp` | S/R | `gl43499/pi_actor.c` | 去 route/profile/global |
| `race/pi_race.h` | S/R | `gl43499/pi_actor.h` 私有 state | handle kind/state |
| `race/threads.cpp` | S/R | actor transaction + named direct calls | route/参数构造进 Kotlin；D3 |
| `race/threads.hpp` | S/R | actor 私有 thread entry | 禁止 JNI 临时引用 |
| `route/backend_contract.hpp` | K/X | 无 C++ catalog | Kotlin wrapper/schema |
| `route/backend_policy.hpp` | K/X | 无 | Kotlin backend choice |
| `route/component_catalog.hpp` | K/X | 仅生成 JNI manifest ID | Kotlin support matrix |
| `route/frontend_contract.hpp` | K/X | 无 | UI/Flow/Lua |
| `route/multicast_waiter_route.cpp` | S/R | `multicast_mechanism.c` | direct call + 单机制门禁 |
| `route/orchestrator.hpp` | K/X | 无 | Flow/Lua |
| `route/pipeline.hpp` | K/X | 无 | Flow/Lua |
| `route/route_api.hpp` | K/X | 无通用 route API | 三 named entry |
| `route/route_controller.cpp` | K/X | 无 | Kotlin selects entry |
| `route/route_controller.h` | K/X | 无 | caller 0 |
| `route/route_lifecycle.hpp` | S/X | 顺序显式写入 actor/mechanism | 不保留 callback framework |
| `route/route_middleware.cpp` | K/X | 无 | Flow/Lua |
| `route/route_middleware.hpp` | K/X | 无 | caller 0 |
| `route/route_policy.hpp` | K/X | 无 | supported/fallback/forge credential hook 进 Kotlin |
| `route/route_status.h` | R/X | 合并到唯一 generated native result | Kotlin 解释；禁止双枚举 |
| `route/select_stack_route.cpp` | S/R | `select_mechanism.c` | direct call + 单机制门禁 |
| `route/select_stack_route.h` | S/R | `select_mechanism.h` 私有 resource | schema 后退役 class |
| `route/tcp_zerocopy_route.cpp` | S/R | `tcp_zerocopy_mechanism.c` | direct call + 单机制门禁 |
| `route/tcp_zerocopy_route.h` | S/R | `tcp_zerocopy_mechanism.h` 私有 resource | schema 后退役 class |
| `session/backend/cve_2026_43499_backend.cpp` | S/K/X | 仅不可拆顺序回填 actor | SELinux overwrite → forge credential → seccomp bypass/verify/retry/fallback进 Kotlin |
| `session/backend/cve_2026_43499_backend.hpp` | K/X | 无 backend policy | caller 0 |
| `session/backend/cve_2026_64560_backend.hpp` | X | 无 stub/空目录 | manifest 无 64560 |
| `session/exploit_session.cpp` | S/K/X | 资源进 typed registry owner | aggregate state进 Kotlin |
| `session/exploit_session.hpp` | S/K/X | 无 `g_exploit_session` | owner/borrow 全映射 |
| `session/handoff_probe.cpp` | K/X | 无 backend probe | Kotlin Finish checks |
| `session/handoff_probe.hpp` | K/X | DTO 进 Kotlin | caller 0 |
| `session/root_child_frontend.cpp` | S/K/X | victim protocol + finish primitive | frontend/handoff policy进 Kotlin/UI |
| `session/root_child_frontend.hpp` | S/X | 私有 victim/finish header | 删除 frontend policy |
| `session/runtime_config.cpp` | K/X | entry validator only | config/default/path进 Kotlin |
| `session/runtime_config.h` | K/X | 无 | caller 0 |
| `session/runtime_paths.h` | K/X | path 作为 typed input | Kotlin/UI 生成 |
| `session/stage_types.hpp` | K/X | 无 | Kotlin state/result |
| `session/victim_context.cpp` | S/R | `victim_protocol.c` owner/termination | transfer tests |
| `session/victim_context.hpp` | S/R | registry 私有 victim resource | 不暴露 class/pointer |
| `session/victim_process.cpp` | S/R/K | protocol child + IPC | 普通 child/verify进 Kotlin |
| `session/victim_process.hpp` | S/R/K | 私有 protocol header | 删除 forge credential / seccomp bypass context |
| `support/decls.hpp` | S/X | owner 窄头 | 禁止新总表 |
| `support/fatal_error.hpp` | X | status+errno+effect | 错误路径测试 |
| `support/native_resource.cpp` | S/R/G | registry/page/actor/victim；纯 cleanup 可 common | 并发/残留 0 |
| `support/native_resource.hpp` | S/R | C11 私有 resource | C++ RAII 不跨 ABI |
| `support/native_result.hpp` | R/G | 唯一 generated result；满足门槛后 common | 不跨 `std::expected` |
| `support/run_state.cpp` | K/X | 无 stdin ACK/global state | Kotlin progress |
| `support/run_state.hpp` | K/X | 无 | caller 0 |
| `support/status.hpp` | S/X | 通用 code 合并唯一 ABI | 禁止第二枚举 |
| `support/time.h` | R/G | consumer 私有；双消费者后 common | timeout policy进 Kotlin |
| `support/util.cpp` | S | 逐 symbol 拆到明确 owner | 禁止整体搬成 util.c |

删除任一行前必须填主指南 §6 的逐 symbol annex；“已分类”不等于允许删除。
