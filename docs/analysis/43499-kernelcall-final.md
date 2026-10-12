# 43499 · 读法② 收官：`op` 校验 + 双向真机证明（round 163）✓

## 改动（沿革 round 162 的遗留 ✓）

```cpp
// main.cpp 的 kernelcall 分支：读参数表 index 1 的 op（script::ArgView ✓）
if (name == "kernelcall" && view->arg_count > 1U) {
    const script::ArgView &op_arg = view->args[1];
    if (op_arg.str.empty() || op_arg.str != "write") { report->refusal = "kernelcall-op-unported"; return; }
}
```

## 双向真机（按本树"守卫须证能失败"的规矩 ✓）

| 用例 | 夹具 | 结果 |
| --- | --- | --- |
| **正例** | `wire-43284-kernelcall.framed`（`op = "write"` ✓） | RC=0 ✓ `calls=7` ✓ `verdict none` ✓ |
| **负例** | `/tmp/wire-43284-kernelcall-bad.framed`（`op = "read"` ✗） | RC=9 ✗ `main: refused (kernelcall-op-unported)` ✓ |

⇒ 守卫**存在** ✓ 且**可失败** ✓；正例与 `writeop` 版**逐项相等** ✓（round 162 ✓）。

## 门禁

- `check` → RC=0，**108 PASS** ✓；`android` → RC=0，0 error / 0 warning ✓；`lint` → 0 ✓。

## 读法② 的完整状态（收官 ✓）

| 项 | 状态 |
| --- | --- |
| surface 行 `kernelcall` | ✓（9 行 ✓，pin 已重生成 ✓） |
| 分派到**同一条**底层路 | ✓（不新增内核行为 ✓） |
| 参数校验（`op`） | ✓（正/负双例 ✓） |
| 纯底层 Lua 可达 | ✓（`calls=7` ✓ 与 writeop 相等 ✓） |
