# 43499 kt-step architecture plan — revision 2

Status: **design-critic approved the final revision with no blocking issues** (2026-10-20), including the glgeneral discovery split and Kotlin backend package refactor. No production code change in this batch. User approved the PI exception for the multicast route and the per-route landing direction.

> **2026-10-21**：批准状态见 [43499-approval-status.md](43499-approval-status.md)（唯一权威）；执行结构见 [execution-architecture.md](execution-architecture.md)。
> 本件（route JNI 家族与包结构）**设计已批准**，实现分批。

## 1. Decision

Routes are not parameter-only. Each route landing gets its own JNI family and its own implementation TU. Only multicast is production-selectable in this batch; select_stack and tcp_zerocopy are non-selectable ENOSYS stubs until their own plans pass.

## 2. Kotlin surface

### 2.1 JNI declarations (exact)

package com.ghostlock.app.engine.ktstep

object NativeMulticastLanding43499Jni {
    external fun create(): Long
    external fun run(handle: Long, params: ByteArray, page: ByteArray, stamp: ByteArray): ByteArray?
    external fun close(handle: Long): Long
}

object NativeSelectStackLanding43499Jni {
    external fun run(params: ByteArray): Int   // always KernelErrno.ENOSYS
}

object NativeTcpZerocopyLanding43499Jni {
    external fun run(params: ByteArray): Int   // always KernelErrno.ENOSYS
}

JNI symbols:

Java_com_ghostlock_app_engine_ktstep_NativeMulticastLanding43499Jni_create
Java_com_ghostlock_app_engine_ktstep_NativeMulticastLanding43499Jni_run
Java_com_ghostlock_app_engine_ktstep_NativeMulticastLanding43499Jni_close
Java_com_ghostlock_app_engine_ktstep_NativeSelectStackLanding43499Jni_run
Java_com_ghostlock_app_engine_ktstep_NativeTcpZerocopyLanding43499Jni_run

All are Kotlin objects (jclass). create returns 0 on failure; run returns null on malformed/refused/failed; close returns 0. The multicast handle is owned by NativeMulticastLanding43499Port; close is idempotent and frees the C++ session.

### 2.2 Ports and route implementations

class NativeMulticastLanding43499Port:
- open(): SyscallResult (handle)
- attempt(params, page, stamp): MulticastLandingResult
- close()

sealed interface MulticastLandingResult:
- Ok(ran, pageStage, pageSendStage, pageErrno, stampResult, stampErrno, stampEqual, pageSlotEqual, actorsStarted, waiter/owner/requeue/consumer, calls, successes, routeDone, disarmResult, disarmErrno, socketClosed, joinTimeout, elapsedNs)
- Errno(code)
- Malformed(reason)

enum RouteSelectability { Production, Stub, Fixture }
fun interface KernelRoute {
    val selectability: RouteSelectability get() = RouteSelectability.Fixture
    fun attempt(...): RouteResult
}

class MulticastLandingRoute(port): KernelRoute {
    override val selectability = Production
}
class SelectStackLandingRoute: KernelRoute {
    override val selectability = Stub
    override fun attempt(...) = RouteResult.Refused("route-not-implemented")
}
class TcpZerocopyLandingRoute: KernelRoute {
    override val selectability = Stub
    override fun attempt(...) = RouteResult.Refused("route-not-implemented")
}

KtOpsInvoker has allowFixture=false by default; it rejects selectability != Production before any JNI call. KtStepEngine gets a constructor injection point `opsInvokerFactory`/`allowFixture` (internal/test-only); production uses allowFixture=false, KtStepEngineTest passes allowFixture=true for FakeKernelRoute. Stub is never allowed, even when Fixture is allowed.

## 3. C++ JNI contract

### 3.1 Multicast

create(): allocates MulticastLandingSession, returns pointer as jlong.
run(handle, params, page, stamp):
- parse params 192-byte fixed layout (revision 7 route plan);
- require page/stamp arrays within bounds; page_len independent of stamp_len;
- compute/verify page/stamp tuple; stamp_equal/page_slot_equal;
- send page exactly via send_page_bounded; on timeout/short/non-socket/peer failure return a result with page_stage=0, actors_started=0;
- run multicast landing order (waiter/owner/consumer + caller requeue), fill result 128-byte layout;
- return jbyteArray or null only for malformed/ENOSYS.
close(handle): joins/clears session; idempotent; returns 0.

### 3.2 Select/tcp stubs

run(params): validate non-null; return KernelErrno.ENOSYS (positive int). No handle, no allocation, no route body. Kotlin port never calls them because selectability=Stub is rejected first; host test asserts no JNI symbol is reached.

## 4. Target graph

common_core (STATIC):
- support/gl_log.c
- support/sha256.cpp
- support/hmac_sha256.cpp
- backend/cve_2026_43499/abi.cpp
- backend/cve_2026_43499/leak/snitch_setup.cpp

support_43284_core (STATIC) -> common_core:
- backend/cve_2026_43284/lib/helper.cpp

syscall_core (STATIC) -> common_core:
- platform/syscalls_43499.cpp

memory_core (STATIC) -> common_core (shared by ktstep and legacy):
- memory/credential_image.cpp
- memory/prepare_plan.cpp
- memory/reclaim_plan.cpp
- memory/shaping_plan.cpp
- memory/write_encoder.cpp
- memory/write_layout.cpp
- memory/multicast_arm.cpp
- memory/payload_page.cpp

ktstep_host_core (STATIC) -> syscall_core, common_core, memory_core:
- platform/native_attempt_43499.cpp (off-Linux ENOSYS path)
- platform/native_multicast_landing_codec.cpp (host-visible params/result codec)
- platform/native_multicast_landing_codec.hpp

glgeneral_core (STATIC, final generic discovery) -> common_core, memory_core:
- steps/discovery_runner.cpp
- platform/native_discover_session.cpp
- platform/native_discover_common.cpp (renamed from native_discover_43499.cpp)

libghostlock_general.so (SHARED) -> glgeneral_core, common_core, log:
- boundary/jni/native_discover_jni.cpp

ktstep_android_core (STATIC, final) -> ktstep_host_core:
- steps/words_region.cpp
- steps/prepare_runner.cpp
- platform/native_prepare_43499.cpp
- platform/native_multicast_landing_43499.*
- select/tcp stub TUs

Interim before phase 6: native_discover_43499.cpp + native_discover_session_43499.cpp are in ktstep_android_core, native_discover_jni.cpp is in gl43499native, libghostlock_general.so is not built. Phase 6 is one atomic CMake commit: remove discovery_runner.cpp/native_discover_*.cpp from ktstep_android_core and native_discover_jni.cpp from gl43499native; add discovery_runner.cpp/native_discover_session.cpp/native_discover_common.cpp to glgeneral_core and native_discover_jni.cpp to libghostlock_general.so; rename the session TU in the same commit. Source-overlap assertion and Android load tests cover before/after.

ktstep_core (INTERFACE) -> ktstep_host_core on host, ktstep_android_core on Android:
- the two source lists above; no legacy units

gl43499native (SHARED) -> ktstep_core, common_core, log:
- boundary/jni/native_attempt_jni.cpp
- boundary/jni/native_prepare_jni.cpp
- boundary/jni/native_multicast_landing_jni.cpp
- boundary/jni/native_select_stack_landing_jni.cpp
- boundary/jni/native_tcp_zerocopy_landing_jni.cpp

gl43499syscalls (SHARED) -> syscall_core, common_core, log:
- boundary/jni/native_syscalls_43499.cpp

legacy43499_core (STATIC) -> common_core, memory_core:
- platform/access_factory.cpp
- backend/cve_2026_43499/access_factory_43499.cpp
- backend/cve_2026_43499/steps/prepare_mapping.cpp
- backend/cve_2026_43499/steps/window_steps.cpp
- backend/cve_2026_43499/steps/route_config.cpp
- backend/cve_2026_43499/steps/window_runner.cpp
- backend/cve_2026_43499/steps/window_attempt.cpp
- backend/cve_2026_43499/landing/multicast_route_linux.cpp
- device/43499/chain.cpp

legacy_session_core (STATIC, shared by legacy JNI products) -> common_core, support_43284_core:
- boundary/jni/api_43499.cpp
- boundary/protocol/session_protocol.cpp
- boundary/protocol/session_handles.cpp
- boundary/protocol/session_object.cpp
- boundary/protocol/ops_43499.cpp

ghostlock43499 (SHARED, legacy only, never packaged) -> legacy43499_core, legacy_session_core, ghostlock_attack_contract:
- boundary/jni/native_43499.cpp
- boundary/protocol/api_43499_ops.cpp

ghostlock_attack_contract (OBJECT):
- contract/attack_contract_reader.cpp; linked once by ghostlock43499, not copied into legacy43499_core

ghostlock_jni (SHARED, legacy, always built/packaged) -> legacy_session_core, support_43284_core, common_core:
- boundary/jni/native_finish.cpp
- boundary/jni/native_memfd.cpp
- boundary/jni/native_raw_esp.cpp
- boundary/jni/native_helper.cpp
- backend/cve_2026_43284/lib/raw_esp.cpp

session_protocol/session_handles/session_object/ops_43499/api_43499 stay only in legacy_session_core; ghostlock_jni and ghostlock43499 must not recompile them.

ghostlock43284 (build.gradle artifact, not a core/CMake target):
- produced by the 43284 pipeline, not by the 43499 split; keep packaging/verify as-is; no source ownership in this plan.

ghostlock_core (STATIC, compatibility only for old tests) -> legacy43499_core + legacy_session_core + common_core + memory_core + support_43284_core; not linked by gl43499native. This keeps jni_session_lifecycle_test (43284 session) and api_43499_test linkable.
ghostlock_attack_contract -> legacy43499_core.

No target links both ktstep_core and legacy43499_core except a dedicated ODR test executable. CMake configure-time assertion covers every target source list: common_core, memory_core, syscall_core, support_43284_core, glgeneral_core, libghostlock_general.so, ktstep_host_core, ktstep_android_core, legacy43499_core, legacy_session_core, ghostlock_jni, ghostlock43499, ghostlock_attack_contract, gl43499native, gl43499syscalls. Only common_core/memory_core/syscall_core/support_43284_core may be shared static libraries; no source path may appear in two lists. A deliberate duplicate-source fixture must fail configure.

## 5. Global-state owners

- g_exploit_session, g_ops_ctx_43499: legacy43499_core only (chain.cpp).
- g_direct_map_end: common_core owner; read-only startup value.
- gl_log state: common_core.
- no new mutable globals in ktstep_core.
- No source is compiled into both ktstep_core and legacy43499_core; common_core and memory_core are the only shared statics. memory_core owns the eight memory units and is linked by both graphs; no duplicate TU.

## 6. Route include whitelist

native_multicast_landing_43499.* may include only:
- platform/syscalls_43499.hpp
- platform/native_attempt_43499.hpp
- platform/native_multicast_landing_codec.hpp (host-visible, route-private)
- platform/native_multicast_landing_43499.hpp (JNI-facing route-private)
- backend/cve_2026_43499/leak/snitch_setup.hpp
- backend/cve_2026_43499/leak/snitch_region.hpp
- backend/cve_2026_43499/memory/write_request.hpp
- backend/cve_2026_43499/memory/write_layout.hpp
- standard C/C++ headers.
It may NOT include KernelAccess43499, access_*, access_factory_43499, Native43499, route_config, window_*, or legacy landing route headers.
JNI TUs (native_multicast_landing_jni.cpp, native_select_stack_landing_jni.cpp, native_tcp_zerocopy_landing_jni.cpp) may additionally include jni.h and their route interface/codec header; the route implementation TU must NOT include jni.h. Stub TUs include only platform/syscalls_43499.hpp, errno, jni.h.
include_firewall_test gets new rules for these TUs.

## 7. Kotlin legacy isolation

Exclude from the production Kotlin source set (app/build.gradle.kts sourceSets main/test kotlin exclude):
- app/src/main/kotlin/com/ghostlock/app/engine/kernelmemory/KernelMemoryEngine.kt
- app/src/main/kotlin/com/ghostlock/app/engine/kernelmemory/JniBackend43499Port.kt
- app/src/main/kotlin/com/ghostlock/app/jni/cve43499/Native43499.kt
- app/src/main/kotlin/com/ghostlock/app/jni/cve43499/Assembled43499.kt
- app/src/test/kotlin/com/ghostlock/app/engine/kernelmemory/KernelMemoryEngineTest.kt
- app/src/test/kotlin/com/ghostlock/app/engine/kernelmemory/JniBackend43499PortTest.kt

Keep Backend43499Port.kt/Facts43499.kt only if no production path imports them; otherwise exclude them too. Move the six files to app/src/legacy43499/kotlin (not a registered source set) and the two tests to app/src/legacy43499/test; do not compile them in debug/release. Add a production-compile guard test that Native43499/Assembled43499 are absent, or a Gradle check that the excluded paths are not in the Kotlin source set. Native43499.ensureLoaded must not exist in production.

## 8. Test mapping

ktstep_core host tests:
syscalls_43499_test, native_attempt_43499_test, discovery_result_test, snitch_child_test, snitch_discovery_test, snitch_policy_test, snitch_region_test, snitch_run_test, snitch_scan_test, snitch_ops_test, snitch_setup_test, snitch_tuning_test, prepare_page_test, prepare_plan_test, prepare_steps_test, reclaim_plan_test, shaping_plan_test, write_encoder_test, write_layout_test, write_request_test, credential_image_test, multicast_arm_test, page_geometry_test, page_slot_test, scan_bounds_test, scan_geometry_test, plus new native_multicast_landing_codec_test, route_selectability_test (Kotlin host), include_firewall_ktstep_test, Cve43499DiscoverFactsTest and Cve43499DiscoverProcessorTest (package backend.cve43499.ktstep.discovery, link ktstep_host_core; assert aliasDelta/pageBase/fakeTask/fakeLock/waiterWord equal current values).

Android/AVD targets: `native_multicast_landing_jni_test` links `gl43499native` and covers only cve43499 families (attempt/prepare/syscalls/multicast/select/tcp), create/run/close, result mapping, select/tcp ENOSYS. Discovery is separate: interim `NativeDiscoverJniCve43499LifecycleTest` links gl43499native; final `NativeDiscoverJniGlgeneralLifecycleTest` links libghostlock_general.so and asserts jni.glgeneral symbols; final run also asserts gl43499native exports none of the jni.glgeneral discovery prefix. Host target: `native_multicast_landing_codec_test` links `ktstep_host_core` only.

The new route tests must cover: multicast params/result codec short/oversize/version, page/stamp equality/mismatch, page send timeout/partial/non-socket/peer-close (zero actors), actor order/result fields, disarm fields; NativeMulticastLanding43499Jni create/run/close handle lifecycle, malformed params, result mapping, close idempotency; select/tcp JNI ENOSYS; MulticastLanding43499Port result mapping; KtOpsInvoker rejects Stub before JNI; Fixture allowed only by FixtureOpsInvoker; no generic NativeRoute43499Port symbol; nm asserts required/forbidden prefixes.

legacy43499_core tests:
access_chain_test, access_route_subclass_test, cve43499_kernel_access_test, kernel_access_test, api_43499_test, jni_ops_43499_test, route_config_test, route_expected_keys_test, route_lifecycle_test, route_policy_test, route_status_test, select_stack_route_test, multicast_policy_test, multicast_landing_test, multicast_window_test, window_attempt_test, window_steps_test, window_exit_paths_test, prepare_mapping_test, step_runner_test, step_targets_test, target_state_test.

common_core tests:
include_firewall_test, hmac_sha256_test, abi_payload_offsets_test.

Remaining existing tests keep ghostlock_core compatibility until reclassified. Expected host count changes must be recorded.

## 9. Build/APK/rollback

- GL_KTSTEP_ONLY default ON; it controls only the 43499 legacy graph, not 43284/ghostlock_jni.
- ON: build gl43499native/gl43499syscalls + ktstep tests; ghostlock43499 not built; ghostlock_jni + ghostlock43284 remain built and packaged; APK excludes libghostlock43499.so.
- OFF: build ghostlock43499 + legacy 43499 tests; ktstep and 43284/ghostlock_jni unaffected.
- prepareGhostlockJniLibs copies: libghostlock_jni.so, libghostlock43284.so, libgl43499syscalls.so, libgl43499native.so, libghostlock_general.so; never libghostlock43499.so.
- verifyJniLibs checks:
  - required: libghostlock_jni.so, libghostlock43284.so, libgl43499syscalls.so, libgl43499native.so, libghostlock_general.so;
  - forbidden: libghostlock43499.so.
- nm -D libghostlock_general.so required: Java_com_ghostlock_app_jni_glgeneral_NativeDiscoverJni_create/discover/close; forbidden: 43499 route/access/KernelAccess symbols.
- nm -D libgl43499native.so:
  - required prefixes after JNI facade migration: Java_com_ghostlock_app_jni_cve43499_ (attempt/prepare/syscalls/multicast/select/tcp); old Java_com_ghostlock_app_engine_ktstep_* allowed only during forwarding window, then forbidden;
  - forbidden after phase 6: Java_com_ghostlock_app_jni_glgeneral_NativeDiscoverJni_ (sole owner is libghostlock_general.so);
  - forbidden: Java_com_ghostlock_app_jni_cve43499_Native43499_, KernelAccess43499, create_kernel_access_43499, and after forwarding removal Java_com_ghostlock_app_engine_ktstep_*.
- nm -D libghostlock_jni.so forbidden exact/mangled substrings: KernelAccess43499, create_kernel_access_43499, MulticastRoute, MulticastLanding, SelectStackRoute, TcpZerocopy, access_factory_43499, route_config, window_runner, window_attempt. Required allowlist: session_protocol/session_handles/session_object/ops_43499 public symbols and native_finish/native_memfd/native_raw_esp/native_helper/raw_esp exports. Full nm/readelf also checks local/static symbols for the forbidden set.
- full nm/readelf/link-map ODR test when both cores are linked.
- rollback: GL_KTSTEP_ONLY=OFF restores legacy link graph; source split remains reversible by reverting CMake.

## 10. Gates

- host/codec/include-firewall/ODR tests;
- Interim AVD JNI lifecycle: load gl43499native, resolve attempt/prepare/syscalls/multicast/select/tcp + interim jni.cve43499 discovery, create/run/close multicast; final AVD JNI lifecycle: load libghostlock_general.so for jni.glgeneral discovery and gl43499native for cve43499 families (two libraries), assert no glgeneral discovery symbol in gl43499native after phase 6.
- AVD existing N2/discover/prepare/page-send;
- multicast landing gate follows route revision 7 + PI exception;
- victim uid=0 only landing proof;
- no UI/config.
