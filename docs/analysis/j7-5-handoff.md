# J7.5 交接单（2026-10-15）

> 目标原文：执行至 J7.5 —— 成功分别用两个 backend 获取 root；可以先内置临时逻辑（只能是 Kotlin）；
> 真实 profile 还需进一步设计。裁定：拼装/写目标/收尾只在 Kotlin，native 只留 JNI 接口；
> 接口不够就改接口，不许往 native 加逻辑；不用 seam 这类临时名。

## 一、已完成（全部有判据，可复跑）

### 1. JNI 边界（D2 装配）

- 两个库各自一个函数面：libghostlock43499.so（13 个导出，向量旧面）、libghostlock43284.so（**35 个薄原语导出**）；
- 参数是 D1 单表生成的 DTO（`com.ghostlock.app.core.generated`，由 `./gradlew exportJniAttackContract` 生成，产物不入库）；
- 契约：**28 组 238 字段**；新增组都按规则写（唯一字段名、字节字段 `GL_ATTACK_BYTES_FIELD(…, N U)`、GROUP/END 包住）；
- 两库导出互不重叠：`./gradlew verifyJniLibs`（期望从源码推导，零手抄）。

### 2. 门禁（可复现的判据链）

    ctest --test-dir build/cmake/host        # 132/132
    ./gradlew checkAll                       # verifyJniLibs + 宿主测试
    ./gradlew deviceGate                     # 38 项真机校验（APK 1010）

deviceGate 的 38 项里包含：分步拼装的每一句（open/socket/surface/landing/bringUp）、Access 线（access/accessInit）、
以及各设备函数的具名拒绝 —— 值都取自真机实测，不写死猜的数字。

### 3. Kotlin 侧（`Assembled43284`，真机自证）

    openTargetStepwise → openEspSocket → buildSurface → buildLanding → bringUpLanding
    → buildAccess → initializeAccessAndRemember → writeImage / writeHook → destroyAccessNow → tearDown

真机逐层结果：`open ✅ / socket=1 ✅ / surface=1 ✅ / landing=1 ✅ / bringUp=true ✅ / access=1 ✅ / accessInit=true ✅ /
patch1 wrote=false refusal=image-write:step-verify-failed`。

即：**Access 线在真机上机械全通（initialize ✅ + write ✅ + read ✅），只差第⑤步回读比较**。

## 二、唯一阻塞项：内核侧那条 SA

见计划 §11.28 / §11.30 / §11.31 / §11.33：

- 树内注释（`steps/pagecache_step_runner.cpp:55-61`）：ESP-in-UDP 的**接收侧在生产环境由 App 的 IpSec 会话持有**（我们绑不上，EADDRINUSE 属正常）；
- 公开 IPsec API 不让指定 SPI/密钥（SPI 由系统分配），而链路要求内核里有**与档案同一 SPI/密钥**的 SA ⇒ 应用权限内自建 SA 不可能成立；
- 当前设备 KernelSU 未加载（`su` 不存在）⇒ 既没有预置 SA，也没有系统权限。

**因此需要用户先定前提**：

| 前提 | 需要 | 下一步 |
|---|---|---|
| A：SA 预置 | 设备已 root / 有人以系统权限按档案建好 SA | 直接跑 J7.5-4：回读应立刻通过 → 载体 → 钩子 → trigger → wait → `id` |
| B：链自建 SA | 系统权限（system app / root / KernelSU 模块） | 先落 §11.31（`espFromFd` 借用语义 + `closeSocketId` 的 borrowed 判定）与 Kotlin 侧系统权限路径 |

## 三、不依赖 SA 的并行工作（43499 侧）

见计划 §11.34：43499 的入口是 `create_kernel_access_43499(profile::RouteId route)`（按 route 取 access）：

1. native 加 `makeAccess43499(route)` / `destroyAccess43499`（新建 `api_43499_device.*` + 挂进 CMake；注意 CMake 源列表是显式列举）；
2. Kotlin 建 `Assembled43499`（与 `Assembled43284` 同型）；
3. 真机跑最小序列（route 取 catalog 一个值，参数由 Kotlin 给）。

route 的取值来自**档案** —— 这正是「真实 profile 还需进一步设计」的实质，也说明两条链的端到端都绕不开档案。

## 四、这段时间学到的硬规则（都付过学费）

1. **看诊断要看全**：`static_assert` 那次白折腾三轮，真因藏在第二行 `note: constexpr evaluation hit maximum step limit`（已加 `-fconstexpr-steps=8000000`）；
2. **改构建文件整行替换**：模糊锚点插行曾把 `add_compile_options(-UNDEBUG)` 的右括号切坏；
3. **插入点用命名空间/类边界**，不用「函数名 + 下一个右花括号」这种局部启发式；跨 TU 的函数**不能定义在匿名命名空间里**（会变内部链接、Android 链接期才报）；
4. **socket 类写法照抄能编译的既有实现**（本 TU 有 `using namespace …`，带 `::` 的限定会解析失败）；
5. **编辑脚本必须自证生效**（断言锚点数、核对写后字节数/符号），不许只打印成功 —— 我因此空改过两次；
6. **接线后同批更新门禁期望**（否则门禁红在你自己身上）；
7. **先取证再动手**：写不落上折腾多轮，最后是树内注释一句话点明的；契约格式的三次失败也是同理；
8. **秘密读后会被清零**（生成的读取器会清源对象）⇒ DTO 不可复用，`Assembled43284` 因此收一个 `paramsProvider` 每次现造；
9. **空路径要写成 {nullptr, 0}**：`half_set()` 定义里有「非空指针 + 长度 0 也算半给」。

## 五、现场状态

- 设备：A301SO（QV770MFGJ1），adb 在线（可能同时挂两条无线通道，用 `-s` 显式指定）；
- 一次性载体：`/data/local/tmp/ghostlock-carrier`（16 KiB、world-writable，探针在用）；`/vendor/lib64/libbinderdebug.so` 连 shell 都读不了；
- 仓库：HEAD 见 git log；`core/` 与 `app/src/main/kotlin` 干净；未提交的只有用户那几处既有改动；
- 计划：`docs/analysis/jni-two-backend-plan.md` §11.6–§11.34（装配点、驱动器、步骤 API、宏语义、真因复盘、SA 结论、43499 入口）。
