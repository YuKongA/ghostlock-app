> 已被 `docs/analysis/43284-refactor-design-v4.md` 取代；仅保留评审历史。

# 43284 重构设计（v3）

- 日期：2026-10-19
- 状态：待 design-critic 复评
- 唯一上游：DFRoot@c274137（v4.6）
- 可选加固：DirtyFrag@de2ab7b，仅按显式条目采用
- 取代：v1/v2；KPLAN §0/§1/§6 的“C++ 不加逻辑”冲突段；DFRP P1/P2/P3 旧会话实现
- 批次计划：docs/plan/43284-reset-batch.md
- 本文件 ≤8 KiB

## 1. 主归属

| 主归属 | 符号 |
|---|---|
| 环境准备 | setup/usage/hex_to_bytes/detect_ko_target（Kotlin）；read_device_versions/select_ko_image/KoImage（C++） |
| app 内核心 | main/exploit/patch_*/marker 轮询（Kotlin）；patch_file_cbc/do_one_write_cbc/compute_iv（C++）；find_hook_target/find_code_cave（KotlinElfPlanner） |
| 子攻击进程 | spawn_crash_dump/read_vendor_content（C++ helper）；createOrphanProcess（C++ trigger）；bootstrap 全部函数（独立载荷） |
| 载荷 | splicehelper/libcxx/ko/pad16/ko_target |
| 收尾 | cleanup/g_libcxx_r/restore（Kotlin）；fadvise_drop（helper 失败时强制） |

每个符号只出现一次；其他位置只写“消费者”。

## 2. C++ 纯层

路径：core/src/backend/cve_2026_43284/lib/，复用现有层，不新建顶层。

禁止 include：jni.h、contract/kernel_access.hpp、boundary/**、device/**。

删除 43284 专用消费者：

- access_params.hpp 的 KernelAccess 继承
- session_object.cpp 的 params_43284 分支
- access_factory.cpp 的 43284 分支
- 契约 AccessParams43284
- Native43284.kt、api_43284、native_43284

保留 43499 通用 Session/NativeProtocol/session_object；B5 只删 43284 分支，不删整文件。补 43499 回归。

## 3. 单 PageCacheAccess

只保留 Kotlin PageCacheAccess（core/access 现有接口）+ 唯一 JNI NativePageCache + BlockWriter。

B4/B5 迁移：

- NativeRawEsp + native_raw_esp.cpp → NativePageCache
- RawEspWriter + KotlinPageCacheWriter → BlockWriter
- Assembled43284/TypedBackendProbe → 阶段编排
- JniPageCacheOps 的 43284 路径删除；43499 保留

静态检查：全仓只允许一个 PageCacheAccess 实现。

## 4. 载荷布局（R1 修正）

| 项 | 偏移/值 |
|---|---|
| /dev/df | 0x00 |
| SELinux ctx | 0x08 |
| attr_exec | 0x1F |
| insmod | 0x34 |
| ko_target 字符串 | 0x47，64B 到 0x87 |
| ko_target_off 偏移字 | 0x88，值 0x47（只读） |
| payload entry | 0x90 |
| first-inst | 0x1C8 |
| jumpback | 0x1CC |
| blob/stage | 468B / 464B |

注入槽 0x47/64B；0x88 不得覆盖。测试：注入后 0x88==0x47，0x90 首指令不变。

helper 由 splicehelper.c static/strip 构建；KO 按 KMI 构建，manifest 记录 sha256。

## 5. 失败与收尾

- Helper 写完 verify 失败：无条件 gl_drop_page_cache(crash_dump64)；或恢复原 helper。不能留改过的页。
- Module 部分写：停止，不写 site，MODULE_PARTIAL。
- Hook：保存原字节后 valid=true；失败先 restore。
- Cleanup：RestoreLedger 幂等；失败 RESTORE_FAILED，不静默。
- 顺序：Wait → Cleanup(restore/drop) → close raw/SA/socket → zero key/seq。
- panic/进程死亡：best-effort，日志标未验证。

## 6. Stage 契约

StageCode：OK、ENV_MISSING、HELPER_VERIFY、MODULE_PARTIAL、HOOK_PARTIAL、TRIGGER_FAILED、WAIT_TIMEOUT、RESTORE_FAILED、CANCELLED。

Stage 接口：

~~~kotlin
interface Stage {
  suspend fun run(): StageResult
  suspend fun abort(code: StageCode)
  suspend fun close()
}
data class StageResult(val code: StageCode, val reason: String, val evidence: Evidence)
data class Evidence(val written: Int, val verified: Int, val site: Long,
  val landing: Long, val firstInstOk: Boolean, val jumpOk: Boolean,
  val restored: Boolean, val source: String)
~~~

Engine 持有 finally；abort 后只允许 close；取消/超时映射 CANCELLED/WAIT_TIMEOUT。

## 7. 资源表补充

| 资源 | 所有者 | 借用者 | 终结点 |
|---|---|---|---|
| RawEsp JNI handle | Kotlin | BlockWriter | Cleanup 后 close |
| Endpoint.sock | C++ endpoint | write_block | close |
| file/pipe fd | C++ 栈 | splice | 返回前 |
| crash_dump 子进程 | C++ helper | helper | waitpid |
| orphan 父/子/孙 | 内核/init | 无 | 孙 sleep 后退出，父不回收 |
| payload/KO buffer | Kotlin | Hook/Module | 阶段结束 |
| site/stage 原字节 | RestoreLedger | Cleanup | 清理后 |

GlKoView/payload 输出为借用视图，owner 是 Kotlin；API 带容量/生命周期。

## 8. 计划取代

v3 取代 v1/v2 全文；KPLAN §0/§1/§6 的 C++ 无逻辑/旧 PageCacheAccess 写入约束；DFRP P1/P2/P3 的 KotlinAttackSession/Backend43284Port/api_43284 实现。

docs/plan/43284-reset-batch.md 记录取代与批次；MASTER-PLAN/ROADMAP 加指针。用户裁定变更留沿革。

## 9. 迁移批次

| 批 | 文件 | 验收 | 回滚 |
|---|---|---|---|
| B1 | lib/env, lib/pagecache | nativeBuildHost、ctest | 删除新文件 |
| B2 | lib/payload/helper/verify/restore | 同上 | 同上 |
| B3 | boundary/jni/cve43284/* | nativeBuildAndroid、符号测试 | 保留旧 JNI |
| B4 | engine/pagecache/stages/*、BlockWriter | app unit + fake access | 保留 TypedBackendProbe |
| B5 | 只删 43284 专用：params_43284/factory/Native43284/JniPageCacheOps 43284 | 全量 build/test + 43499 回归 | 恢复备份 |
| B6 | include 防火墙 + marker 扫描 | 0 命中 | 回滚测试 |
| B7 | 设备门禁 A | AGENTS §8 归档 | — |

命令：

~~~text
./gradlew nativeBuildHost nativeTestHost
./gradlew nativeBuildAndroid
./gradlew :app:testDebugUnitTest :app:assembleDebug
~~~

## 10. API 补全

- gl_env_select_ko 返回借用 GlKoView，owner 为 manifest
- gl_helper_install(path, offset, bytes, len, verify, err)
- gl_helper_exec(offset, path, fd, mode, out_status)
- gl_trigger_orphan(delay_ms, out_pid, out_stage)；父不回收，孙 sleep 退出
- gl_pagecache_write_range(..., seq_in, seq_out, written_out)
- gl_payload_build(layout, ko_target, out, cap, size_out)

## 11. 测试矩阵

| 层 | 负例 |
|---|---|
| C++ | 空/越界/容量不足/部分写/errno |
| JNI | 符号/参数/错误 stage |
| Kotlin | fake access、Helper/Module/Hook 中途失败、取消、cleanup 幂等 |
| 防火墙 | 纯库不含 jni.h/kernel_access/boundary |
| marker | /data/local/tmp/.gl_、DfrootExperiment、旧 ghostlock_lkm_*、/dev/df 探针为 0 |
| 43499 | 编译 + 单测回归 |
