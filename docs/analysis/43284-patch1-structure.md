# patch#1 的完整结构（工具 505-566 行）与我的差距（2026-10-15）

## 工具的做法（与我的分步拼装同构）

```cpp
PageCacheDoorLinux patch_door(params.patch_target.bytes, params.patch_target.length);   // 开目标
const int patch_fd = patch_door.open_target();
if (patch_fd < 0) { printf("cannot open the patch target %s (errno %d)", kPatch1Target, errno); return 4; }

RealSpliceIo  patch_surface(real_splice_io(), patch_fd);   // 页面缓存 I/O 面绑该 fd
PageCacheWrite patch_window(patch_door, patch_surface);    // 写体
PageCacheLanding patch_landing(patch_window);              // 落地
Access patch_access(&patch_landing);                       // 访问对象
patch_access.initialize(params);                           // 初始化（要 sa）
phase_patch1(patch_access, params);                        // 逐块写 helper
spent_sequence = patch_window.next_seq();                  // 序号接续（内核防重放窗口）
```

**结论：我 Kotlin 侧的分步拼装（door → surface → landing → Access → initialize → 逐块写）与它一一对应 ✓**
（我用的是 door + 拼接面 + 落地 + Access 这条既有路径，本来就对）。

## 两个关键差距

1. **门失败要带 errno**：工具 `printf(... errno %d)`；我这边只 log 了返回值 0 ✗ ——
   而 errno 恰好能区分「EACCES（权限）」与「ENOENT（路径）」与「其它」，是**下一步唯一需要的信息**；
2. **carrier 侧工具带兜底**：载体开不了时它用 `CrashDumpBridge`（补丁后的 helper 代为读/拼）✓ ——
   而 **patch#1 自身没有兜底** ✓ 说明 patch#1 的目标**必须**本进程能开（它正是「先 patch 它、再用它当桥」的那一个）；
   也就是说：**patch#1 的门开不了，这条链没有别的入口** ⇒ 必须先把 errno 拿到看清楚。

## 下一步（唯一需要的动作）

让 Kotlin 的 `openTargetStepwise` 把失败原因（errno 或 native 给的具名码）带回来并打进日志：

- 若 errno=EACCES：门需要写权限（与 §8.8 的观察一致），则要换开法（只读 + ESP 写）；
- 若 errno=ENOENT/其它：说明路径或调用方式不对，按具名信息再定。

拿到这一个数字，patch#1 就能继续；不看它继续试是浪费。
