# tools/avbcheck：最后一个未迁移的 Makefile 构建（2026-10-15）

## 现状

```text
tools/avbcheck/
  Makefile     ← NDK aarch64 静态可执行 + 主机可执行 + 主机单测（自成一体）✗ 未接 Gradle
  README.md    src/    tests/    build/
```

Makefile 的目标（照它自己的注释）：

```text
make             NDK aarch64 静态可执行 → build/avbcheck-aarch64
make host        主机可执行（离线交叉核对）
make test        主机单测 + avbtool fixture 交叉核对
make clean
```

## 两个问题

1. **未接入 Gradle/CMake** ✗ —— 与项目规则「构建逻辑一律 Gradle KTS」冲突，也是用户要求迁移的对象；
2. **硬编码本机 NDK 路径** ✗：

```make
NDK ?= $(ANDROID_NDK_HOME)
ifeq ($(strip $(NDK)),)
NDK := /Users/nickji/Library/Android/sdk/ndk/30.0.16248370      ← 机器相关路径进了仓库 ✗
endif
```

本仓已有**正确的解析写法**可照抄：`build.gradle.kts:47-64`（环境变量 → `local.properties` → 具名 GradleException）。

## 迁移方案（照既有风格）

```text
1. tools/avbcheck/CMakeLists.txt：两个目标 —— 设备端（NDK aarch64 静态）与主机端（离线核对 + 单测）；
2. 根 build.gradle.kts 加任务 avbcheck（host）与 avbcheckDevice（NDK），并把主机单测接进 checkAll / ctest；
3. 删掉 Makefile（或保留为注释说明已迁），NDK 路径一律走既有解析；
4. 判据：./gradlew avbcheck 通过；ctest 仍 132/132（或把新单测接进总数）。
```

## 迁移账本（当前）

| 目标 | 状态 |
|---|---|
| 根级 make 流程（host/check/android） | ✅ 已迁（Makefile 已删，见 build.gradle.kts:196） |
| LKM 构建 / 拷贝 / 校验 | ✅ 已有 Gradle 任务（buildLkmImages / copyLkmIntoAssets / verifyLkmLedger） |
| LKM manifest | ✅ 在树里；那句过时提示已修（7f9eb7b9） |
| **tools/avbcheck** | ⬜ **未迁**（本文件） |
| tools/lkm/ghostlock/Makefile | ◻ 保留为 kbuild 片段（内核要求） |
| core/lib/lua/Makefile(+src) | ◻ 第三方上游，保持不动 |