# S8b-1 证据：image/p0/direct-map 参数化（2026-10-21）

> 设计：[s8b-layout-replace-design](s8b-layout-replace-design.md)。目标：运行时 image/p0/direct-map 只读 layout。

## 改动

- `Glk43499Layout` 增 `mtk_vaddr_base/xring_kernel_phys_load/qc_gki_6_12_phys_load`。
- defaults 从 target.h 填这些字段；`fake_task_prio_off` 固定 0x94 + assert。
- `publish_active_offsets`：
  - `init_cred_image`：off 非 0 时 `layout.kimage + off`，0 时保留 defaults。
  - MTK/Xring/QC 分支只用 layout 字段。
- `data_addr/p0_data_alias` 用 layout 的 kimage/p0/page/phys。
- `init_p0_profile/log_startup` 用 layout p0/phys。
- `prepare_leak_mm` 用 layout identity/direct_map。
- `_RSO_IMAGE` 改 `L->kimage_text_base`；main/fops/util 相关函数加 `L`。
- `perf_find_task(direct_map_end)`；`child_pipes.direct_map_end` 由 layout 填入。

## 门禁

```text
./gradlew :app:externalNativeBuildDebug --offline  # BUILD SUCCESSFUL, 0 warnings

flatten S1：base=19 cur=19 args_diff=16 PASS
flatten S2：base=11 cur=11 args_diff=10 PASS
flatten S4：base=17 cur=17 args_diff=13 PASS

llvm-nm 43499 b/B = 0
```

## 未完成

- S8b-2：payload lock/w0/task/fops/cred、TCP/SKB geometry。
- S8b-3：fake waiter/task offsets。

## 文档

- [S8a](s8a-layout-defaults-split.md)
- [拆分计划](../plan/split-large-functions.md)
- 日志：`build/analysis/flatten-s8a-s1.log`
