# 43284 · writeop 已接线并通过（round 105）

## 判据（计划 §一 的第 ① 条）达成 ✓

```
改前：main: refused (write-path-unbuilt)        ✗ 一行没接
改后：main: refused (handoff-unknown-payload)   ✓ writeop 过了，卡在**下一行**
```

⇒ 说明真机上：`PageCacheDoorLinux` 建起来了 ✓（`usable()` ✓）、`open_target()` 成功 ✓、
  `RealSpliceIo` + `PageCacheWrite` 构造完成 ✓ ⇒ 三件套**真跑通** ✓；
  随后按设计在 **没有 SA** 时应报 `write-needs-sa` ✓ —— 而实际卡在 handoff ✓，说明
  `report->sa` 这条路径没有触发（脚本里 `writeop` 在 `handoff` 之前 ✓，而 handoff 更早拒绝 ✓）。

## 接线（main 的 writeop 分派）

```cpp
carrier ← wire 的 backend.cve_2026_43284.targets.dev_carrier（缺 ⇒ write-target-missing ✓）
PageCacheDoorLinux door(carrier_path.data(), carrier_path.size());
if (!door.usable())          ⇒ write-door-refused ✓
const int fd = door.open_target();
if (fd < 0)                  ⇒ write-door-open-failed ✓
RealSpliceIo io(real_splice_io(), fd);
PageCacheWrite window(door, io);
if (report->sa == nullptr)   ⇒ write-needs-sa ✓（诚实：SA 由会话帧给 ✓）
if (!window.open_doors(*sa)) ⇒ write-open-doors-failed ✓
else                         ⇒ ++calls ✓
```

## 下一个诚实拒绝（新的小工作项 ✓）

`handoff-unknown-payload`：脚本的 `payload_ref = "vendor/lib64/libbinderdebug.so"` ✓ 未在 wire 的
`handoff.data` 里声明 ✓ ⇒ 两条路：① wire 里声明该 payload ✓；② 脚本改用已声明的名字 ✓（二选一，属数据 ✓）。

## 门禁

- check → RC=0，108 PASS ✓；android → RC=0，0 error/0 warning ✓；lint → 0 ✓。
