# S4 · R3 实施设计：HOCON 布局重排（owner-qualified + selection + backend 分组）

> 上游：`config-wire-redesign-plan.md`（R3、§5.1、§6.1）。前置：R1/R2/R2c/R2c-2/R4 均已完成。
> 顺序：**R3 先于 R2b**（维护者 2026-10-05）。

## 1. 目标布局（新 canonical）

```hocon
ghostlock {
  schema_version = 3
  release = "5.15.189-…"
  selection { backend = "cve_2026_43499"  steps = "w1_w3"  terminal = "root_child" }
  common { kernel_major = 5  fallback_route = "none" }
  platform { abi { kernel{…} task_struct{…} cred{…} offset{…} } }
  backend {
    cve_2026_43499 {
      steps = "w1_w3"
      kernel { compact_waiter = true  kernelsnitch_collisions = 8  mm_struct_sz = 1024 }
      cred { copy_size = 176  usage_value = 256  caps_count = 3  caps_value = …  ref0_image = … }
      offset { slide_nfulnl_logger = …  slide_boot_ids = … }
      route { multicast_waiter { waiter_off = 96 … } }
      execution { … }
    }
    cve_2026_43284 { steps = "pagecache_write"  lkm_path = "/data/local/tmp/helper.ko"  wait_timeout_ms = 15000 … }
  }
  countermeasure { vivo_vr_guard { tracepoint_funcs = … } }
}
```

- **HOCON 路径必须与 wire 路径一一对应**（R2 的 owner-qualified 命名即权威）：`platform.abi.*`、`backend.<id>.*`、`common.*`、`countermeasure.*`；
- **`schema_version` 仍为 3**（不新增版本号）；**无 HOCON v4**；

## 2. 兼容（唯一迁移点仍是 Kotlin）

- 解析器**同时接受**：① 新的 `ghostlock{…}` 包裹布局；② **旧扁平布局**（含 `backend.steps`、`kernel_phys_*`、`fallback.to`、`kernelsnitch.*`）；
- 归一化（alias 表）在**解析期**完成：旧键 → 新 owner-qualified 路径；未识别的旧键**报错带路径**（不静默丢弃）；
- 用户导入的旧 profile 不改也能跑（`UserProfileStore`/`LegacyProfileConverter` 路径复用同一归一化）。

## 3. 机械迁移 + **扁平化等价门禁**

63 个内置 conf 机械迁移；**安全门禁**：对每个内置文件，
`flatten(旧文件) ≡ flatten(新文件)`（在 alias 表映射下逐键逐值相等），任何差异必须解释或 FAIL。
例外（非恒等映射）必须在设计里显式登记：`backend.steps`→`selection.steps`/`backend.<id>.steps`；`kernel_phys_load|offset`→`platform.abi.kernel.*`；
`fallback.to`→`common.fallback_route`；`kernelsnitch.collisions`→`backend.cve_2026_43499.kernel.kernelsnitch_collisions`；`cred`/`offset` 的平台/私有拆分按 R2 重命名表。

## 4. `index.conf` backend 矩阵

新增 `backends = [ { id = "cve_2026_43499"  available = true } { id = "cve_2026_43284"  available = true } … ]`，
驱动 UI 可用性；**与 native catalog 对拍**（新增导出或在既有 agreement test 中扩展），不一致即 FAIL。

## 5. 逐文件清单

- **资产**：`app/src/main/assets/profile/*.conf`（63 个主体 + `index.conf` + 5 个 `include` 片段改为新布局的片段键）；
- **Kotlin**：`HoconSupport`（包裹解包）、`ProfileMerger`/`ProfileResolver`、`AndroidProfileConfigController`、`BuiltinProfileCatalog`、`UserProfileStore`（同一归一化）、高级设置路径键；
- **extractor**：`tools/extract_rs/src/report.rs` 输出新布局（其 `flatten_conf(generated) ≡ flatten_conf(bundled)` 测试必须继续通过）；
- **测试**：Kotlin「新旧布局都过」+「旧键归一」+「未识别旧键报错」；extractor `cargo test`；native `profile_manifest_v3_test` 不变（wire 不动）。

## 6. 不变量

- **wire 不动**（`schema == 3`、路径、类型、canonical）；native 代码除（可选的）catalog 导出外不改；
- 43499/43284 真机行为不变（取值逐项一致）；旧 profile 可读（归一化）；
- 无新增可变全局；零告警；防火墙不变。

## 7. 门禁

| 门槛 | 判据 |
|---|---|
| 扁平化等价 | 63 个文件逐键逐值等价（脚本 + 测试） |
| host / NDK / lint | 全绿（native 未改则不变） |
| Kotlin | `:app:testDebugUnitTest`、`:profile-core:test`（新旧布局、归一、矩阵对拍） |
| extractor | `cargo test --release`（生成 ≡ 内置） |
| 真机 43499 | 冷启 PASS（新布局资产 → 同一 wire → 行为一致） |
| 真机 43284 | app-call `EXIT=0` + `profile_resolved` 与 R4 一致 |

## 8. 风险

- **键名漂移**会静默改变生效值 → 用扁平化等价门禁兜底；
- 别名表遗漏会让旧 profile 报错（fail-closed，用户可见）→ 覆盖全部现存旧键（由 63 个文件 + `Credential/Execution` 片段枚举）；
- `include` 片段（5 个无 `schema_version` 的文件）在新布局下的键前缀必须与主体一致，否则合并后路径不匹配。
