# ADR-0019：43499/43284 纯 C 核心与 C JNI 边界

- 日期：2026-10-21
- 状态：**Accepted；2026-10-23 registry 修订为 Proposed，随 JNI v4 候选批准后生效**。
- 关联计划：[43499-43284-cpp-parameterization-plan.md](../plan/43499-43284-cpp-parameterization-plan.md)
- 关联事实：[43499-43284-core-inventory.md](43499-43284-core-inventory.md)

## 背景

`AGENTS.md` §4 曾规定 C++23/RTTI；2026-10-21 更新已划除该条文，并明确“测试分支、纯 C 可用”。当前 `app/src/main/cpp/glk43499`（约 130 KB C）与 `glk43284`（约 30 KB C）是已确认可用的实现，直接以 C11 编译（`CMAKE_C_STANDARD 11`），并已产出可对拍的 28 个函数机器码基线。

若在参数化/JNI 化之前全量改写 C++23：

1. 28 个攻击函数的机器码无法逐函数对拍，`cmp_disasm` 失去基线意义；
2. C 的 `_Atomic`、指定初始化、`naked` 汇编、可变长数组与 C++ 语义冲突，重写会引入真实行为差异；
3. 工程量与风险集中在“语言迁移”，而用户当前目标是“参数化 + 所有权 + JNI 基础”。

## 撤回理由（2026-10-21）

`AGENTS.md` §4 原“C++23、RTTI、静态 libc++”条文已划除（`<del>…</del>`），用户 2026-10-21 明确测试分支可自由改码、纯 C 可用。因此原“C11 例外”不再存在：P1–P5 用纯 C 是**工程选择**，不需要规范例外。

## 决策

1. **P1–P5 纯 C**：核心与 JNI 边界均用 C11；新增 `glk43499_jni.c`/`glk43284_jni.c`，`JNIEXPORT` 导出。
2. **编译选项**：`-Wall -Wextra -Wconversion -Wsign-conversion`，新码零告警。
3. **JNI 边界**：只传 POD/句柄；不做 C++ 异常/RTTI/模板；跨边界类型用 `static_assert` 钉布局。
4. **范围**：仅限本计划列出的 `app/src/main/cpp` 文件；不扩散到 `core/src` 或 Kotlin。
5. **后续**：P6 后若用户要求再评估语言迁移；当前无 C++ 迁移计划，T-LANG 不登记。
6. **registry 候选例外**：每个 JNI DSO 可有一个固定容量、C11 实现的 handle registry，作为唯一新增可写进程状态；只在 PI 窗口外访问。slot 私有布局、fork/close/borrow 协议与 CBN 以 JNI v4 §5 为权威。

## 备选与否决理由

| 备选 | 否决理由 |
|---|---|
| 立即全量 C++23 | 28 函数机器码基线失效；130 KB 重写无法与当前已验证实现逐函数对拍；风险高于收益 |
| 只写 C++ 包装、核心仍是 C，但不登记例外 | 事实上的规范偏离被隐藏，违反 AGENTS §1 的记录义务 |
| 另建实验副本 | 复制 160 KB 代码，产生第二权威与漂移；用户目标是对现有实现参数化，不是维护两份 |
| 保持 C11 且不设偿还 | 例外永久化，违反 F2 技术债预判 |

## 后果

- 正面：保留可对拍基线；参数化/所有权/JNI 分批验证；无语言层。
- 代价：无 RTTI/RAII；资源释放靠显式 owner 表；JNI 边界需手写 `JNIEXPORT` 与 POD 断言。
- 约束：除获批后由 manifest 生成的每 DSO registry 符号外，C 核心不得有可写全局；不得在 PI 窗口内访问 registry、分配、打印或间接分派。
- 技术债：T-LANG 已消解（AGENTS 条文删除 + 用户纯 C 授权）。

## 批准

| 项 | 内容 |
|---|---|
| 批准人 | 用户（YG） |
| 批准方式 | 2026-10-21 指令：测试分支、git 备份、可自由改码、纯 C 可用 |
| 批准日期 | 2026-10-21 |
| 范围 | `app/src/main/cpp` 的 43499/43284 核心与 JNI |

> 沿革：2026-10-21 曾以 Proposed 要求批准 C11 例外；同日 AGENTS §4 删除 C++23 条文、用户明确纯 C 可用后，本 ADR 改为 Accepted（纯 C 选择）。

> 2026-10-23 候选沿革：JNI v4 为 typed handle 提议 per-DSO registry，取代“可写全局绝对为空”。代价是锁、fork/close 状态和固定容量；偿还为并发矩阵、BSS manifest、残留检查和 PI 窗口隔离。用户批准前本条不生效。

## JNI v4 amendment 批准槽

| 项 | 内容 |
|---|---|
| amendment 状态 | Accepted |
| 批准人/指令/日期 | 用户；“按照计划”；2026-10-23 |
| 生效依据 | JNI v4 D0-R5 critic 阻断 0 后的实现指令 |
