# 43499 C11 资源原语头文件契约批

- 日期：2026-10-26
- 级别：L（跨 JNI/native 契约）
- 状态：用户已明确要求“core 移到 legacy2 后，从零按 Page/Actor 结构完整写 `.h`”；本批只生成未接线的 C11 头文件。
- 取代关系：本批修正 `43499-jni-v5-atomic-primitives-design.md` 中被评审指出的两处边界错误；不批准其余 rev6/rev8 实现内容。

## 目标

在 `app/src/main/cpp/gl43499/core/` 建立自包含的 C11 契约：Placement 是不可拆的定位事务；Landing 成功消费 Placement 并产生 Page；Actor 在 prepare 时绑定一个物理 Primitive，execute 不再接收 route/tag；Actor 成功接管并拥有 Page，旧借用方案已取代。

## 改动清单

- 新增 `abi.h`、`result.h`、`placement.h`、`page.h`、`primitive.h`、`actor.h`、`api.h`。
- 更新批准状态表一行，登记本批仅为 header-only。
- 更新 `full-process-uml.md` 的 43499 候选图，使它与新契约一致。
- 不改 CMake、JNI、Kotlin、legacy、legacy2、victim、Finish、ksud 或任何实现文件。

## 关键决策

1. 当前构建是 C11，因此公开契约只使用固定宽度 POD 与 opaque `uint64_t` handle。
2. 每个输入结构以 `glk43499_abi_header` 开头；版本为项目统一版本3，保留位必须为零。
3. `prepare_placement` 一次完成顺序敏感的定位；不再暴露虚假的 `allocate → scan` 物理分界。
4. `land_page` 调用期借用 bytes；Native 不保存调用方指针。
5. `prepare_actor`深拷贝PrimitiveSpec并在线程创建前绑定；成功消费Page，Actor成为Page唯一owner；
   `execute_actor`无Primitive参数。
6. 调用状态、物理 effect、资源 state 正交返回。
7. handle是不透明Native对象token；不使用registry slot，也不公开fd、pid、pthread、业务route、
   W1/W2/W3、profile、victim或Finish。raw token误用不属于受支持契约。

## Primitive API 的 CBN

| 方案 | 当前入口触点 | 每增一种 Primitive | execute 窗口分派 | Kotlin/JNI 面 | 结论 |
|---|---:|---:|---:|---:|---|
| 一个 tagged `PrimitiveSpec` | 1 个 `prepare_actor` | union 增 1 支、prepare 前置解析增 1 支 | 0；prepare 时解析一次并在线程创建前绑定 | 1 套 | 本批采用 |
| 三个具名 prepare 入口 | 3 个 prepare | 新增 1 个入口及 JNI/Kotlin 映射 | 0 | 每种机制各一套 | 当前重复面更大 |

翻转条件：若实现无法用直接函数/静态状态在创建线程前完成绑定，或反汇编发现 execute/worker 仍读取 kind、出现 switch/间接分派，则废弃 tagged union，改成具名 prepare 入口；不得以窗口内动态分派换取 API 数量减少。

## 验证

- 七个头文件分别以 C11 和 C++23 `-fsyntax-only -Werror` 自包含编译。
- 聚合 `api.h` 做固定宽度、ABI version、handle-zero、枚举值与函数签名静态断言。
- 验证 legacy2 和本批文件无 include 依赖。
- 不宣称链接、APK、真机或攻击效果验证；这些属于后续实现批。

