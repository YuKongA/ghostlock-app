# LKM 构建迁移的正确形态（2026-10-15 读 Makefile 后）

## Makefile 内容（很小，是 kbuild 片段）

```make
obj-m += ghostlock.o
CFLAGS_ghostlock.o := -Os -fno-asynchronous-unwind-tables -fno-unwind-tables
all:    $(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm64 LLVM=1 modules
clean:  $(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm64 LLVM=1 clean
```

注释写明：模块是**逐页写进目标**的，所以体积重要（-Os + 无 unwind 表）✓

## 关键事实：kbuild 模块不能用 CMake 直接编

out-of-tree 内核模块**必须由内核自己的 Kbuild 驱动**（`make -C $KDIR M=$CURDIR …`）——这是内核的接口，
不是「遗留构建脚本」。因此「迁移到 CMake/Gradle」的正确含义是：

1. **保留** `tools/lkm/ghostlock/Makefile` 作为 **kbuild 片段**（内核要求，且是接口的一部分）；
2. **新增 Gradle 任务**（构建逻辑一律 KTS）驱动它：解析 DDK（KDIR）→ 调 kbuild → 收集产物 → 记**字节数与 sha256**；
3. 同时给 `app/build.gradle.kts:243` 那句**过时提示**一个真实目标：要么实现 `lkmKmiManifest` 这个 Gradle 任务，
   要么把提示改成指向存在的入口（现在的 `make -C core lkm-kmi-manifest` 指向已删除的 Makefile ✗）。

## 为什么这一步与主链相关

移植链的 LKM 阶段（设计 §九）要求「产物可核对」：退役树那次是 **12304 字节 / sha256 d557fe11…**，
所以构建入口必须能产出并记账 —— 没有它，后面「等 LKM 起来」这一步就没有可信的输入。

## 下一步

1. 找 DDK/KDIR 在本仓的既有解析方式（AGP 的 ndkDirectory 一类，或 local.properties）；
2. 写 Gradle 任务（名先定 `lkmBuild`；manifest 若要则 `lkmKmiManifest`），产物进仓库内 build/，日志里打字节数与 sha256；
3. 改 243 行提示指向新任务；
4. 三门 → 提交。