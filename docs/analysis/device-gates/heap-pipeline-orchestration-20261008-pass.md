# 真机门禁：堆管线编排（43499 · prepare_page）· 2026-10-08

> 级别：**攻击关键路径改动**（新增编排）⇒ 按 AGENTS 需真机门禁 + 本记录 ✓
> 设备 `QV770MFGJ1`（A301SO · Android 15 · `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`）
> 代码：`core/src/backend/cve_2026_43499/memory/{prepare_page,prepare_steps,prepare_plan,reclaim_plan,shaping_plan,page_geometry,heap_geometry,heap_ops,heap_ops_linux}.hpp|cpp`

## 1 判据与结果

| 判据 | 结果 |
|---|---|
| 12 阶段在真机上按序跑完 | **通过** ✓（`at=7` = 停在 `LeakLoop` ✓ 与桩搜索一致 ✓） |
| 设备页尺寸实测（L3） | **4096** ✓（与设备 `getconf PAGESIZE=4096` 一致 ✓） |
| 区域与设备一致性校验（K2） | `region=32768` ✓ 校验 `ok` ✓ |
| 描述符账目 == 回收规则 | `opened=9`（2+4+2+1 ✓）· `closed=5`（pre 2 ✓ + post 1 ✓ + spray 每区域 1 ✓ + **leak 1** ✓） |
| 失败路径收尾（无碰撞） | `status=plan-missing`（取不到命中 ✓ ⇒ 具名）· 子进程**零残留** ✓（`ps` 计数 0 ✓） |
| 探针退出码 | **0** ✓ |

## 2 原始输出（两次，第一次暴露缺陷）

**第一次（修复前）**

```
prepare page probe
  page=4096 region=32768 groups: pre=2 spray=4 post=2
  status=plan-missing at=7 opened=9 closed=4 errno=0
prepare page probe: PASS
EXIT=0
```

**缺陷（真机发现 ✓）**：`opened=9 / closed=4` ⇒ 差 5 中 4 个是**故意保留的回收载体** ✓（spray 其余 ✓），但 **leak 描述符在提前返回时无人关闭** ✗ —— 回收走查**有意跳过** leak 组 ✓，而早停路径**直接 return** ✗ ⇒ **真机最可能发生的失败路径上泄漏一个 fd** ✗。

**第二次（修复后）**

```
prepare page probe
  page=4096 region=32768 groups: pre=2 spray=4 post=2
  status=plan-missing at=7 opened=9 closed=5 errno=0
prepare page probe: PASS
EXIT=0
```

## 3 证伪（守卫能失败 ✓）

- 造错：把早停路径的 leak 关闭删掉 ⇒ `make -C core check` = **2** ✓，失败行 `FAIL: including the leak descriptor, which nothing else will close` ✓；还原 + `rm -f` 产物 ⇒ **0** ✓。
- 另：早停路径忘记 `Snitch::reset()` ⇒ 对应断言红 ✓（上一批已记 ✓）。

## 4 范围（证明什么 / 不证明什么）

**证明** ✓：编排的**阶段顺序**、**描述符账目与回收规则**、**失败路径收尾**（含 reset / 杀子 / 归还）在**真机**上成立；landing 的每个调用在该内核可用（另见 heap landing 探针 ✓）。

**不证明** ✗：① **地址搜索未实现** —— 本轮桩报“无碰撞” ⇒ 跑的是**失败路径** ✓，**成功路径在真机上尚未跑过** ✗；② `prepare_skb_payload`（页内写）与其失败路径未移植 ✗；③ fork 出的子进程在本轮**立刻 `_Exit(0)`** ✓（探针局部 landing ✓）—— 真实的子进程工作（spray/leak）未实现 ✗；④ 未做同机旧二进制对拍（仅旁证口径 ✓）。

## 5 复现命令

```sh
make -C core prepare-page-probe
adb -s QV770MFGJ1 push build/native/prepare_page_probe /data/local/tmp/
adb -s QV770MFGJ1 shell '/data/local/tmp/prepare_page_probe; echo EXIT=$?'
adb -s QV770MFGJ1 shell 'getconf PAGESIZE'
```
