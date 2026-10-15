# LKM 构建迁移：KDIR（DDK）的输入条件（2026-10-15）

## 已查实

1. **NDK 的解析在本仓已有现成写法**（可照抄给 LKM 用）：
   `build.gradle.kts:47-64` —— 先取环境变量 `ANDROID_NDK_HOME`，再取 `ANDROID_NDK_ROOT`，最后 `local.properties` 的 `ndk.dir`；
   找不到就抛 `GradleException("NDK not found; set ANDROID_NDK_HOME or ndk.dir in local.properties")` ✓；
2. `local.properties` 存在（含 SDK 位置）✓；
3. **本机没有 DDK / 内核构建树** ✗ —— SDK 目录下只有 `ndk`，没有 `android13-5.15` 之类的 DDK；
   而 kbuild 需要 `KDIR`（内核源码/头 + Kbuild）才能跑 `make -C $(KDIR) M=… modules`。

## 因此 Gradle 任务的形态（照 NDK 的现成写法）

```text
lkmBuild（新 Gradle 任务）：
  1. 解析 KDIR：环境变量 GL_KDIR → 否则 local.properties 的 kdir → 否则抛 GradleException 并写明怎么设（与 NDK 那句同风格）；
  2. 调 kbuild：make -C <KDIR> M=tools/lkm/ghostlock ARCH=arm64 LLVM=1 modules
     （LLVM=1 用 NDK 的 clang；NDK 路径照 build.gradle.kts 的现有解析拿）
  3. 收集产物 ghostlock.ko → 拷进仓库内 build/ → 日志打**字节数与 sha256**（退役树基准：12304 字节）；
  4. lkmKmiManifest（可选）：生成 KMI manifest —— 这正是 app/build.gradle.kts:243 那句过时提示要指向的东西。
```

## 现状的诚实结论

- 任务**可以现在就写**（形态明确、可照抄 NDK 的解析风格）；
- 但**本机跑不出产物**（没有 DDK）⇒ 任务会以「KDIR 未设」具名失败 ✓（这本身是对的：比静默不产好）；
- 产物与 sha256 一旦有 DDK 就能与退役树基准对照（12304 / d557fe11…）。