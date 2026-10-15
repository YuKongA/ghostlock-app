# Makefile→CMake 迁移的剩余项（2026-10-15 勘察）

## 现状

```text
仍存在的 Makefile：
  tools/lkm/ghostlock/Makefile     ← **LKM 构建**（用户要求迁移的对象）
  tools/avbcheck/Makefile          ← 工具
  core/lib/lua/Makefile(+src/)     ← 第三方（Lua 上游，默认不动）

已迁移：
  build.gradle.kts:196 注明「取代旧 Makefile 路径（make -C core host/check/android，Makefile 已删除）」✓
```

## 一个真实缺口（本轮发现）

`app/build.gradle.kts:243` 仍在**引用一个已删除的 Makefile 目标**：

```text
"missing LKM manifest: " + file + " (regenerate with: make -C core lkm-kmi-manifest)"
```

⇒ 提示语让使用者去跑 `make -C core lkm-kmi-manifest`，但那个 Makefile 已经删了 ✗ ——
也就是说 **LKM KMI manifest 的生成能力目前没有 Gradle/CMake 入口**，只有一句过时的提示。
这与「所有 Makefile 构建迁移到 CMake / 构建逻辑一律 Gradle KTS」的要求直接冲突，也是移植 LKM 阶段（§九）的前置：
没有 manifest 就没有可核对的 `ghostlock.ko`。

## 下一步（不依赖 SA 帧，可与主链并行）

1. 读 `tools/lkm/ghostlock/Makefile`（它怎么编、依赖哪些 DDK 变量、产物与 sha256 怎么记）；
2. 写成 **CMake 目标 + Gradle 任务**（构建逻辑放 KTS；DDK 由 AGP 的 ndkDirectory 一类解析；产物进仓库内 build/）；
3. 同时补上 `lkm-kmi-manifest` 的 Gradle 入口（或把提示语改成存在的入口），让 243 行那句提示不再指向已删除的东西；
4. 三门（宿主 + Android + 门禁）→ 提交。