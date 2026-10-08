# ADR-0010：Lua 与 C++ 资源模型（作用域 · 句柄生命周期 · GC 边界）

- 状态：Proposed（待用户批准，2026-10-08）
- 索引：[README.md](README.md)
- 日期：2026-10-08
- 相关：**ADR-0008**（Lua 运行期与沙箱）、**ADR-0009**（脚本信任与能力面硬线）、`handoff-payload-b1b2-runtime.md` §B2.3、`handoff-payload-plan.md` §10 **D30**、`pi-window-constraints.md`

## 背景（Context）

- **D30（2026-10-07）只定义了句柄形态**：`{kind, index, generation}`、表有界 64、由探针产生、不暴露裸指针。**生命周期、作用域与 GC 边界是缺口** —— 本节补齐。
- C++ 侧资源是 **backend state 成员**：`src/core/backend/cve_2026_43499_state.hpp:25-40`（`addresses` / `heap` / `race` / `victim` / `parked_victim` / `parked_victim_cmd`），其构造与销毁由 **RAII guard** 固定顺序完成：`src/core/pipeline/pipeline.hpp:26-46`（`BackendStateGuard` 构造 → `state_construct`，析构 → `state_destroy`）。
- 若把 C++ 资源的回收交给 Lua GC，会与 **PI 窗口的确定性**要求冲突（窗口内禁分配/禁间接分派/禁越界 syscall，见 `pi-window-constraints.md`）。

## 决策（Decision）

- **修订（2026-10-08 裁定）**：**`cfg`（只读快照）不属句柄面** —— 它是 stage 前注入的**只读快照**（脚本只能读字段），**不产生句柄、不受本 ADR 的句柄规则约束**；句柄仍只由**宿主探针**产生（见 [lua-surface-design](../analysis/lua-surface-design.md) §1）。

### 1. 所有权原则

- **C++ 独占所有权**；**Lua 只持句柄**（POD，无指针、无引用）。
- 每个资源**必须**归属三者之一：**宿主（窗口作用域）**、**op（op 作用域）**、**Lua GC（脚本作用域）**；**不存在第四种归属**。

### 2. 作用域三分类

| 作用域 | 资源 | 所有者 | 回收 | 时序 |
|---|---|---|---|---|
| **窗口作用域** | `race` / `parked_victim` / PI 内存 / `addresses` / `heap` | **宿主**（backend state 成员，`cve_2026_43499_state.hpp:25-40`） | **RAII guard**（`pipeline.hpp:26-46`） | **固定**（construct → … → destroy）；脚本**不可干预** |
| **op 作用域** | op 内临时对象 | op | **返回即回收**；**默认不产生句柄** | op 调用内 |
| **脚本作用域** | Lua 表 / 字符串 / 数字 | **Lua GC** | GC | **不映射任何 C++ 资源** |

### 3. 八条硬规则

1. **不得自造句柄**：句柄只能由**宿主探针**在**窗口外**产生；脚本不能构造或修改句柄字段。
2. **不得延长寿命**：无 `retain` / `pin`；持有句柄**不阻止**宿主回收。
3. **不得跨 scope**：窗口 / op / 脚本三域句柄**互不通用**；op 结束即失效。
4. **不得跨插件**：句柄与其产生时的**插件环境绑定**，粘到别的 env 即拒。
5. **不得写入文档或 wire**：句柄不是配置数据（不落 GLKv3、不进 manifest）。
6. **不得在窗口内使用**：脚本只在**操作之间**运行；**窗口内零 Lua**。
7. **不得登记 C++ 回调 / hook / 线程**：脚本不能在 C++ 侧注册任何回调、hook 或线程。
8. **不得借句柄推导裸地址**：写原语只接受**句柄 + 强类型参数**，**不接受裸地址**。

### 4. 错误语义（五条具名拒绝 + 立即终止，非 UB）

| 原因 | 触发 |
|---|---|
| `handle-stale` | `generation` 不匹配（含**回绕后**的旧句柄） |
| `handle-kind-mismatch` | `kind` 与调用点不符 |
| `handle-out-of-range` | `index ≥ 64`（表**有界 64**） |
| `handle-scope-denied` | 跨 scope / 跨插件 |
| `param-invalid` | 类型错（含 `nil`、函数、表塞进标量位） |

- 全部为**具名拒绝 + 立即终止**（不是 UB、不是静默 0）；**`generation` 回绕后旧句柄必须失效**。

### 5. GC 边界

- 句柄是 **POD**：**无 `__gc` 释放路径**；Lua GC **不回收** C++ 资源。
- Lua 堆硬上限（`lua_setallocf`）只约束 **Lua 自身**内存，**与 C++ 资源无关**。
- **C++ 回收不回调 Lua**：无终结器、无跨语言析构、无「对象销毁通知」。

## 判据（白盒 / 黑盒 / 极端值）

- **白盒**：宿主 API 签名**无裸指针 / 地址类型**（`grep` 计数 = 0）；容量与代际单测**含回绕**与 `index = 64/65`；全仓**无 `__gc` 释放路径**。
- **黑盒**：过期 / 错 `kind` / 越界 / 跨插件句柄 ⇒ **具名拒绝且立即终止**（退出码非 0 + 具名原因）。
- **极端值**：64 用满 · 代际回绕 · 句柄粘到别的插件 env · op 表塞函数 / 表 · `handle` 传 `nil`。

## 后果（Consequences）

- **得**：脚本**无法**驱动写原语写任意地址（D30 的目的）；C++ 资源生命周期与 PI 窗口确定性**解耦**。
- **代价**：宿主必须实现**句柄表 + 代际 + 作用域校验**；且 **「窗口内零 Lua」必须由宿主侧结构性保证**（不能只靠约定/文档）。

## 沿革

- **D30（2026-10-07）**：只定义句柄**形态**（`{kind, index, generation}`、有界 64、探针产生、不暴露裸指针）。
- **本 ADR（2026-10-08）**：补**所有权 / 作用域三分类 / 八条硬规则 / 五条具名拒绝 / GC 边界**；**不改** D30 的形态结论。

## 相关（References）

- **ADR-0008**（Lua 运行期与沙箱）· **ADR-0009**（脚本信任与能力面硬线）。
- `handoff-payload-b1b2-runtime.md` §B2.3（类型化句柄 D30）· `handoff-payload-plan.md` §10 D30 · `pi-window-constraints.md`（窗口约束）。
