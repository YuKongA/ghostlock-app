# 43284 重构设计（v4）

- 日期：2026-10-19
- 状态：待 design-critic 复评
- 唯一上游：DFRoot@c274137（v4.6）
- 可选加固：DirtyFrag@de2ab7b，仅 helper 失败 drop
- 取代：v1/v2/v3 全文；DFRP P1/P2/P3 的旧会话实现；KPLAN 用户裁定不变

## 0. 约束保持

KPLAN §0/§1 用户裁定保持不变：

- 探测/解析/cave/plan/写入编排在 Kotlin；
- 写入继续走现有 PageCacheAccess；
- C++ 只保留 syscall/纯函数原语，不新增 backend 逻辑。

v4 不覆盖该裁定；只细化阶段、资源与删除清单。

## 1. 主归属

| 主归属 | 实现位置 |
|---|---|
| 环境准备 | Kotlin EnvironmentStage；KMI/carrier/payload 选择全在 Kotlin |
| app 内核心 | Kotlin PageCacheEngine、KotlinElfPlanner、BlockWriter |
| 子攻击进程 | Kotlin HelperStage/TriggerStage + JNI helper syscall + bootstrap 载荷 |
| 载荷 | Kotlin PayloadBuilder；libcxx/helper/KO 资源 |
| 收尾 | Kotlin CleanupStage/RestoreLedger |

C++ 只有：

- gl_esp_open/close/write_block；gl_helper_exec/read；gl_fadvise_drop。
- 无 KernelAccess、无会话、无 plan/payload 逻辑。

## 2. 单 PageCacheAccess 与删除清单

唯一路径：

~~~text
Kotlin PageCacheAccess（core/access 现有接口）
  └─ BlockWriter（Kotlin）
       └─ NativeRawEsp（唯一 JNI）
            └─ C++ gl_esp_* syscall
~~~

| 旧文件 | 新归属 | 批次 |
|---|---|---|
| NativeRawEsp.kt + native_raw_esp.cpp | 保留为唯一 JNI/syscall | 保留 |
| lib/raw_esp.* | 保留为 C++ syscall 库 | 保留 |
| RawEspWriter.kt | 合并进 BlockWriter | B4 替换，B5 删除 |
| KotlinPageCacheWriter.kt | 合并进 BlockWriter | B4 替换，B5 删除 |
| Assembled43284.kt | 删除，阶段编排替代 | B5 |
| TypedBackendProbe.kt | 删除，PageCacheEngine 替代 | B5 |
| JniPageCacheOps 43284 路径 | 删除；43499 保留 | B5 |
| Native43284.kt/api_43284/native_43284 | 删除 | B5 |
| access_params.hpp/interface.hpp | 只留 interface.hpp 的 AccessParamsPod；access_params.hpp 删除 | B5 |
| session_object.cpp 的 params_43284 分支 | 删除分支，保留 43499 通用 Session | B5 |
| access_factory.cpp 的 43284 分支 | 删除；43499 保留 | B5 |
| contract 的 AccessParams43284 | 删除字段/生成物 | B5 |

静态检查：实现/适配器白名单 + AST/字节码扫描；允许列表只含 core/access 的 PageCacheAccess 实现与 NativeRawEsp 适配器；B5 输出命中清单。

AccessParamsPod 唯一定义在 core/src/backend/cve_2026_43284/interface.hpp；access_params.hpp 的 KernelAccess 类 B5 删除。

## 3. 载荷布局（R1）

| 项 | 偏移/值 |
|---|---|
| /dev/df | 0x00 |
| SELinux ctx | 0x08 |
| attr_exec | 0x1F |
| insmod | 0x34 |
| ko_target 字符串 | 0x47，64B 到 0x87 |
| ko_target_off | 0x88，值 0x47（只读） |
| entry | 0x90 |
| first-inst | 0x1C8 |
| jumpback | 0x1CC |
| blob/stage | 468B / 464B |

注入槽 0x47/64B；0x88 不得覆盖。测试：注入后 0x88==0x47，0x90 首指令不变。

## 4. 资源与收尾

| 资源 | 所有者 | 借用者 | 终结点 |
|---|---|---|---|
| SA/socket | Kotlin Environment | BlockWriter/Trigger | Cleanup 后关闭 |
| key/seq | Kotlin | 写/trigger | Cleanup 后 zero |
| RawEsp JNI handle | Kotlin | BlockWriter | 写后 close |
| Endpoint.sock | C++ | write_block | close |
| file/pipe fd | C++ 栈 | splice | 返回前 |
| crash_dump 子进程 | C++ helper | helper | waitpid |
| helper 原字节 | RestoreLedger | Cleanup | 清理后 |
| orphan 父/子/孙 | 内核/init | 无 | 孙 sleep 退出；父不回收 |
| payload/KO buffer | Kotlin | Hook/Module | 阶段结束 |
| site/stage 原字节 | RestoreLedger | Cleanup | 清理后 |

Helper 失败：HelperStage 写入前按 helper padded 长度保存 crash_dump 原字节（RestoreLedger.helper_orig/len，上限 64 KiB）；保存失败停止，不写 helper。verify 失败写回完整范围；写回失败再 gl_fadvise_drop；仍失败进入 RESTORE_FAILED，记录 helper 范围与 errno，不宣称页面已恢复。

RESTORE_FAILED 资源终态：

| 资源 | 动作 |
|---|---|
| raw handle | close |
| SA/socket | close |
| key/seq | zero |
| LKM window | 保留到下一次运行/用户处理，owner=Engine |
| helper 页 | dirty |
| 后续攻击阶段 | 停止 |

Cleanup 顺序：Wait → restore site/stage → helper 恢复 → close raw/SA/socket → zero key/seq。

## 5. Stage 状态机

~~~kotlin
enum class StageCode { OK, ENV_MISSING, HELPER_VERIFY, MODULE_PARTIAL,
  HOOK_PARTIAL, TRIGGER_FAILED, WAIT_TIMEOUT, RESTORE_FAILED, CANCELLED }
enum class StageState { New, Running, Aborting, Closed }
interface Stage {
  suspend fun run(): StageResult
  suspend fun abort(code: StageCode)
  suspend fun close() // idempotent
}
data class StageResult(val code: StageCode, val reason: String, val evidence: Evidence)
data class Evidence(val written: Int, val verified: Int, val site: Long,
  val landing: Long, val firstInstOk: Boolean, val jumpOk: Boolean,
  val restored: Boolean, val source: String)
~~~

合法转换：New→Running→Closed；Running→Aborting→Closed；abort 后只允许 close；close 幂等。Engine 持有 finally。

## 6. C++ 纯函数 API

| 函数 | 参数 | 返回 |
|---|---|---|
| gl_esp_open | spi, encap_port, sender_port | handle/err |
| gl_esp_write_block | handle, path, offset, iv16, seq | bool/err |
| gl_esp_close | handle | void |
| gl_helper_exec | offset, path, fd, mode | status/err |
| gl_helper_read | path, offset, out16 | bool/err |
| gl_fadvise_drop | path | bool/err |

约束：无逻辑编排；buffer 借用；errno/stage 返回；无全局状态。

gl_helper_exec 的 vfork child 只允许 dup2/execve/_exit 等 async-signal-safe 调用；禁止 JNI、Kotlin、堆分配、锁。

## 7. 迁移批次

| 批 | 文件 | 验收 | 回滚 |
|---|---|---|---|
| B1 | C++ gl_esp_* + include_firewall_test | nativeBuildHost/ctest + 防火墙 0 命中 | 删除新文件 |
| B2 | C++ gl_helper_* + gl_fadvise_drop | 同上 | 同上 |
| B3 | NativeRawEsp 薄导出/ABI 测试 | nativeBuildAndroid + 符号 | 保留旧 JNI |
| B4 | BlockWriter/Stage/PageCacheEngine | app unit + fake access | 保留 TypedBackendProbe |
| B5 | 按 §2 删除旧 writer/probe/43284 专用符号 | 全量 + 43499 回归 + 单实现扫描 | 恢复备份 |
| B6 | include 防火墙 + marker 扫描 + AttackFlowGuard 更新 | 0 命中 | 回滚测试 |
| B7 | 设备门禁 A | AGENTS §8 归档 | — |

命令：

~~~text
./gradlew nativeBuildHost nativeTestHost
./gradlew nativeBuildAndroid
./gradlew :app:testDebugUnitTest :app:assembleDebug
ctest --test-dir build/cmake/host -R include_firewall --output-on-failure
~~~

命令对应：

| AGENTS §8 | 当前正式入口 | 判据 |
|---|---|---|
| make -C core check | nativeBuildHost + nativeTestHost | 构建退出码 0；ctest 全量；-Wall/-Wextra/-Wconversion 零告警 |
| make -C core android | nativeBuildAndroid | NDK 构建退出码 0；零告警 |

当前树无 core/Makefile；Gradle 为正式入口。B1 新增 include_firewall_test，B1/B2 每批跑 ctest -R include_firewall；B6 全量回归。marker 扫描命令：rg -n '/data/local/tmp/\.gl_|DfrootExperiment|ghostlock_lkm_|/dev/df' app/src/main core/src，期望 0；AttackFlowGuardTest 新增 .gl_、ghostlock_lkm_、/dev/df 禁用串。

## 8. 测试

- C++：空/越界/容量/部分写/errno；
- JNI：符号/参数/错误 stage；
- Kotlin：fake access、Helper/Module/Hook 中途失败、取消、cleanup 幂等；
- 防火墙：纯库不含 jni.h/kernel_access/boundary；
- marker：/data/local/tmp/.gl_、DfrootExperiment、旧 ghostlock_lkm_*、/dev/df 探针为 0；更新 AttackFlowGuardTest；
- 43499：编译 + 单测回归；
- 设备：冷机、固定 CPU、单 route、KernelSU 未加载。

## 9. 计划取代与沿革

v4 取代 v1/v2/v3 全文；DFRP P1/P2/P3 旧会话实现；KPLAN 用户裁定不变，仅细化阶段。

MASTER-PLAN/ROADMAP/43284-reset-batch 已加指针；KPLAN/DFRP 头部已加沿革。v1/v2/v3 头部标“被 v4 取代”。
