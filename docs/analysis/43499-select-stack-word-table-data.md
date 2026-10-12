# 43499 · 词表数据化：不把 6.1 的数字搬进 5.15（round 169）✓

## 发现（对照即刻可见 ✓）

```
快照（6.1 词表 ✓）：task = waiter 词 **8**、lock = 词 **9**
本机（5.15 ✓，几何**已在真机验证** ✓）：task_offset = 0x30 ⇒ 词 **6**、lock_offset = 0x38 ⇒ 词 **7**
⇒ 两套内核词号**不同** ✗ ⇒ 直接搬快照的数字 = 把 6.1 的布局用到 5.15 上 ✗
```

## 改法（结构 ✓，不是打补丁 ✗）

```cpp
// SelectStackConfig：11 个词号全部变成**文档字段** ✓
word_task / word_lock / word_tree_pc / word_tree_right / word_tree_left /
word_pi_pc / word_pi_right / word_pi_left / word_wake_prio / word_deadline / word_ww_ctx

// 组装函数返回 (词号, 值) 对 ✓ —— 词号**无处硬编码** ✓
struct SelectStackWord { std::int32_t word; std::uint64_t value; };
select_stack_compact_words(config, std::array<SelectStackWord, 11> &out)
```

## 判据（本轮 ✓）

```
build/host/select_stack_route_test ⇒ "select-stack route: word table and fd_set sites hold" ✓  TEST_RC=0 ✓
check → RC=0，108 PASS ✓；android → RC=0，0 警 ✓；lint → 0 ✓
测试里刻意用**本机几何推出的词号**（task 6 / lock 7 ✓）跑一遍 ⇒ 证明换一套词号也能工作 ✓
```

## 为什么这一轮值得（诚实 ✓）

- 它把移植里**最容易埋雷的一处**（跨内核词号 ✗）在接线之前就变成数据 ✓；
- 也正合 AGENTS 的「数字走文档」✓：本批没有把任何别的内核的数字写死在代码里 ✓；
- 下一步（接线时）只需**按设备核出那 11 个词号**并填进文档 ✓，代码不动 ✓。
