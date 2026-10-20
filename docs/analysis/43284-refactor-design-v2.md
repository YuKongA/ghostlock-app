> 已被 `docs/analysis/43284-refactor-design-v4.md` 取代；仅保留评审历史。

> 已被 `docs/analysis/43284-refactor-design-v3.md` 取代；本文件仅保留评审历史。

# 43284 重构设计（v2）

- 日期：2026-10-19
- 状态：待 design-critic 复评
- 唯一上游：DFRoot@c274137（v4.6）
- 可选加固：DirtyFrag@de2ab7b，单独标注，不参与默认验收
- 取代：43284-refactor-design.md；DFRP 的 43284 实现条款；KPLAN §13–§15 冲突段

## 1. 分类轴

每个符号只有一个主归属；其余只写“消费者”。

| 主归属 | 含义 |
|---|---|
| 环境准备 | 选择/解析，不写页缓存 |
| app 内核心 | Kotlin 编排 + 纯函数写 |
| 子攻击进程 | crash_dump / orphan / bootstrap |
| 载荷 | 写入目标文件的字节/模块 |
| 收尾 | 恢复、丢弃、关闭、清零 |

## 2. 符号主归属

### 环境准备

| 符号 | 主归属 | 消费者 |
|---|---|---|
| setup/usage/hex_to_bytes | Kotlin EnvironmentStage | 无 |
| detect_ko_target | Kotlin CarrierPolicy | ModuleStage |
| read_device_versions | C++ gl_env_read_versions | Kotlin |
| select_ko_image/KoImage | C++ gl_env_select_ko | ModuleStage |

### app 内核心

| 符号 | 主归属 | 消费者 |
|---|---|---|
| main/exploit | Kotlin PageCacheEngine | 无 |
| patch_helper | Kotlin HelperStage | CleanupStage |
| patch_ko | Kotlin ModuleStage | CleanupStage |
| patch_hook | Kotlin HookStage | CleanupStage |
| find_hook_target/find_code_cave | Kotlin KotlinElfPlanner | HookStage |
| patch_file_cbc | C++ gl_pagecache_write_range | Helper/Module/Hook/Cleanup |
| do_one_write_cbc | C++ gl_pagecache_write_block | write_range |
| compute_iv | C++ gl_pagecache_compute_iv | write_block |
| marker 轮询 | Kotlin WaitStage | TriggerStage |

### 子攻击进程

| 符号 | 主归属 | 消费者 |
|---|---|---|
| spawn_crash_dump | C++ gl_helper_exec | Helper/Module |
| read_vendor_content | C++ gl_helper_read_vendor | ModuleStage |
| createOrphanProcess | C++ gl_trigger_orphan | TriggerStage |
| bootstrap main 及 read_prefs/pref_true/adopt_zygote_env/should_ro/set_partitions_ro/disable_modules/late_load_running/run_ctx/touch | 子进程载荷 bootstrap | 无 |

### 载荷

| 符号 | 主归属 | 消费者 |
|---|---|---|
| splicehelper start_c | 载荷 splicehelper | crash_dump64 |
| libcxx clone worker/grandchild | 载荷 libcxx.S | init |
| dirtyfrag.ko | 载荷 KO | insmod |
| pad16 | C++ gl_payload_pad16 | 所有载荷写入 |
| ko_target | 载荷数据槽 | ModuleStage 注入 |

### 收尾

| 符号 | 主归属 | 消费者 |
|---|---|---|
| cleanup | Kotlin CleanupStage | 无 |
| g_libcxx_r | Kotlin RestoreLedger | CleanupStage |
| patch_file_cbc 恢复 | C++ gl_restore_range | CleanupStage |
| fadvise_drop | 可选 B 加固 | CleanupStage；A 默认不启用 |
| close socket/zero key/seq | Kotlin CleanupStage | 无 |
| /dev/df* marker | Kotlin CleanupStage | 无 |

## 3. 当前 → 目标差异

| 当前 | 目标 |
|---|---|
| access_params.hpp 继承 KernelAccess | 删除；参数 Kotlin DTO + POD |
| interface.hpp/access_params.hpp 同名 AccessParams | 只留 AccessParamsPod |
| session_object.cpp 的 params_43284 | 删除；帧解析归 Kotlin |
| access_factory.cpp 43284 分支 | 删除 |
| 契约 AccessParams43284 | 删除 |
| Native43284.kt/api_43284/native_43284 | 删除 |
| JniPageCacheOps/NativeProtocol session | 删除 |
| RawEspWriter + KotlinPageCacheWriter | 合并为一个 Kotlin writer |
| TypedBackendProbe | 删除，替换为阶段编排 |

## 4. 分层与路径

```text
Kotlin app/src/main/kotlin/com/ghostlock/app/engine/pagecache/
  PageCacheEngine.kt
  PageCacheAccess.kt
  stages/{Environment,Helper,Module,Hook,Trigger,Wait,Cleanup}Stage.kt
  plan/KotlinElfPlanner.kt
  write/BlockWriter.kt

JNI core/src/boundary/jni/cve43284/
  native_env.cpp
  native_pagecache.cpp
  native_helper.cpp
  native_payload.cpp
  native_verify.cpp

C++ core/src/lib/43284/ namespace ghostlock::lib::cve_2026_43284
  env.cpp pagecache.cpp payload.cpp helper.cpp verify.cpp restore.cpp
```

C++ 禁止 include：jni.h、contract/kernel_access.hpp、boundary/**、device/**。

## 5. 阶段契约

```kotlin
interface Stage {
  suspend fun run(): StageResult
  suspend fun abort(reason: StageCode)
}
enum class StageCode { OK, ENV_MISSING, HELPER_VERIFY, MODULE_PARTIAL,
  HOOK_PARTIAL, TRIGGER_FAILED, WAIT_TIMEOUT, RESTORE_FAILED, CANCELLED }
data class StageResult(val code: StageCode, val evidence: Evidence)
data class Evidence(val written: Int, val verified: Int, val site: Long,
  val landing: Long, val firstInstOk: Boolean, val jumpOk: Boolean,
  val restored: Boolean)
```

顺序：

```text
Environment → Helper → Module → Hook → Trigger → Wait
  → Cleanup(restore/drop) → close raw/SA/socket → zero key/seq
```

Cleanup 在 finally，幂等；资源关闭在 Cleanup 之后。

## 6. 纯函数 API

| 函数 | 参数 | 返回/容量 |
|---|---|---|
| gl_env_read_versions | GlVersionOut* | bool |
| gl_env_select_ko | android/major/minor, manifest | GlKoView |
| gl_pagecache_open | spi/ports | GlEndpoint |
| gl_pagecache_write_block | endpoint,path,offset,iv,seq | bool |
| gl_pagecache_write_range | endpoint,path,offset,bytes,len,seq* | written/err |
| gl_pagecache_compute_iv | key,old,desired,out16 | bool |
| gl_pagecache_close | endpoint | void |
| gl_payload_pad16 | in,len,out,cap | padded/err |
| gl_payload_build | layout,ko_target,out,cap | size/err |
| gl_helper_install | helper,size | bool |
| gl_helper_exec | offset,path,fd,mode | status |
| gl_helper_read_vendor | path,offset,out16 | bool |
| gl_module_validate | image,size,kmi | bool |
| gl_verify_range | path,offset,expected,size | bool |
| gl_restore_range | path,offset,original,size | bool |
| gl_drop_page_cache | path | bool（可选 B） |
| gl_trigger_orphan | delay_ms | pid/err |

所有 out 缓冲带容量；sequence 由 Kotlin 持有并传入/回传；C++ 无全局状态。

## 7. 资源与生命周期

| 资源 | 创建 | 所有者 | 借用者 | 终结点 |
|---|---|---|---|---|
| SA/socket | Kotlin Environment | Kotlin | write/trigger | Cleanup 后关闭 |
| raw handle | C++ pagecache_open | Kotlin | BlockWriter | Cleanup 后 close |
| file fd | C++ write_block | C++ 栈 | splice | 返回前 close |
| pipe fd | C++ write_block | C++ 栈 | vmsplice/splice | 返回前 close |
| payload bytes | Kotlin | Kotlin | Hook | Hook 结束 |
| KO image | Kotlin | Kotlin | Module | Module 结束 |
| site/stage 原字节 | Kotlin RestoreLedger | Kotlin | Cleanup | 进程结束/清理后 |
| crash_dump 子进程 | C++ helper_exec | C++ 调用栈 | helper | waitpid |
| helper pipe | C++ helper_exec | C++ 栈 | crash_dump | waitpid 后 close |
| orphan 父/子/孙 | C++ trigger | 内核/init | 无 | 孙 sleep 后退出；父不回收 |
| helper 页缓存 | 内核 | 内核 | crash_dump | drop（可选） |

vfork 子进程只允许 async-signal-safe 调用；orphan 有意不回收。

## 8. 失败与收尾

- 每阶段维护 written/partial/valid；
- Helper 写完 verify 失败：HELPER_VERIFY，drop helper 页缓存；
- Module 部分写：停止，不写 site；MODULE_PARTIAL；
- Hook 原字节保存后 valid=true；失败先 restore；
- Cleanup 幂等；失败记 RESTORE_FAILED，不静默；
- 进程被杀/panic：best-effort，日志标未验证。

## 9. 载荷清单

以 A（DFRoot v4.6）为准：

| 项 | 值 |
|---|---|
| libcxx blob | 468B |
| libcxx stage | 464B |
| entry | 0x90 |
| first-inst | 0x1C8 |
| jumpback | 0x1CC |
| ko_target | 0x88，64B |
| helper | splicehelper.c static/strip 构建 |
| KO | 按 KMI 构建，sha256 manifest |

删除 472B 旧口径；DfrootExperiment 与 /data/local/tmp marker 全删。

## 10. 迁移批次

| 批 | 内容 | 验收 |
|---|---|---|
| B1 | C++ env/pagecache + 单测 | nativeBuildHost、ctest |
| B2 | payload/helper/verify/restore + 单测 | 同上 |
| B3 | JNI 薄导出 + ABI 测试 | nativeBuildAndroid、符号测试 |
| B4 | Kotlin Stage + fake access + 失败/取消测试 | :app:testDebugUnitTest |
| B5 | 删除 access_params/interface/factory/session_object/Native43284/JniPageCacheOps | 全量 build/test |
| B6 | include 防火墙 + marker 扫描 | 0 命中 |
| B7 | 设备门禁（A 基准） | AGENTS §8 归档 |

每批失败回滚上一批；B5 前不得删旧实现。

## 11. 测试矩阵

- C++：空/越界/容量不足/部分写/错误码；
- JNI：符号、参数、返回值、错误 stage；
- Kotlin：fake access、阶段失败、取消、cleanup 幂等；
- 防火墙：纯库不含 jni.h/kernel_access.hpp/boundary；
- marker 扫描：/data/local/tmp/.gl_、DfrootExperiment 为 0；
- 设备：冷机、固定 CPU、单 route、KernelSU 未加载。
