# 门的 errno 要在哪里捕获（2026-10-15）

## 勘察结果

- native 侧**没有** `device_open_target` 这个原语 ✗；门是在**构建拼接面时**打开的：
  `core/src/device/43284/chain.cpp:124` → `io(backend::cve_2026_43284::landing::real_splice_io(), door.open_target())`；
- `open_target()` 只是把缓存好的 fd 返回（`api_43284_device.cpp:409` 一带的 door 类实现）；
- 因此**失败原因（errno）必须在 `door.open_target()` 调用之后立刻捕获**，
  在 `device_resolve_target`（或构建拼接面那个函数）里把它带出去 —— 否则 errno 会被后续调用覆盖。

## 要改的地方（很小）

1. native：在打开门之后立刻 `const int saved = errno;`，把 `saved` 与一个具名短串（`open:eacces` / `open:enoent` / `open:other`）
   作为出参回传（照我已有的 `probeReadPath` 的写法 —— 那套具名机制已经在用）；
2. `resolveTarget` / `makeSurface` 的导出加一个出参（`jlongArray` 里多带一格，或复用现有的 out 数组）；
3. Kotlin：`Assembled43284` 在门失败时把这两个值打进日志（`typed: patch1 door=0 errno=… reason=…`）。

## 为什么值得单独做这一步

`errno` 是唯一能区分下面两种情况的数字：

- `EACCES` ⇒ 门需要写权限（与 §8.8 的观察一致）⇒ 要换开法（只读打开 + 走 ESP 写）；
- `ENOENT` / 其它 ⇒ 路径或调用方式不对 ⇒ 按具名信息再定。

**拿到它之前，任何 patch#1 的重试都是盲试。**
