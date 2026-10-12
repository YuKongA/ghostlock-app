# 43499 · 修复轮搬移的前置：先上提 6 个值（round 139 实测）

## 实测（本轮照指令搬了一次 ✓）

```
搬移本身成功（79 行；1560 → 1571 行 ✓），但新位置编译失败 ✗：
  error: use of undeclared identifier kAliasBase / alias_image_delta / kOrder3 / kChunks（其后还会有更多）
⇒ 因为原块引用的这些值都声明在**步骤建页那个作用域**里（main.cpp:657-687 ✓）
```

## 因此搬移前必须先做一步（上提 ✓）

把这些声明从"步骤建页块"**上提到函数作用域**（赋值语句留在原处即可 ✓）：

| 值 | 现位置 | 说明 |
| --- | --- | --- |
| `kOrder3` / `kChunks` | main.cpp:657-658 ✓ | constexpr ✓ |
| `kAliasBase` | main.cpp:679 ✓ | constexpr ✓ |
| `alias_image_delta` | main.cpp:680 附近 ✓ | 依赖 `phys_load`/`phys_offset` ✓（也需上提 ✓） |
| `payload_delta` | 已在准备之前 ✓（round 122 上提过 ✓） | 可能已可用 ✓ |
| `init_cred_image` / `selinux_enforcing_image` | 步骤建页块内 ✓ | 需上提 ✓ |
| `page_inputs`（`slots.*` ✓） | 步骤建页块内 ✓ | 需上提（或把 `slots` 的取值改为从 `page_inputs` 复制到函数级变量 ✓） |

## 之后照旧 ✓

- 搬移 806–884 → 主轮之后 ✓；把 `execute` 换成窗口四件套 ✓；
- 判据：**两轮修复均 `status=0/0`** ✓；随后回到 `uid=0` ✓。

## 现状

- 树：android RC=0（0 警 ✓）、check **108 PASS** ✓、lint 0 ✓（搬移已回滚 ✓，文件回到 1560 行 ✓）。
