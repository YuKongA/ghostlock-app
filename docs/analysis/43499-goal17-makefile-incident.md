# 43499 · 第 16-17 轮：Makefile 事故与 §8 证伪（切片 1 现已被门禁守住）✓✓✓

## 一、事故（✓，逐字证据 ✓）

```
现象：`./build/host/page_slot_test` 打出的却是 **`a2: PASS …`** ✗（= 那个路径上的二进制是 a2 的产物 ✓）。
原因：插入新构建规则时，**a2 的配方行被吸进了 `page_slot_test` 的配方** ✗ ——
      recipe 行以 Tab 起 ✓、空行**不**终止配方 ✓ ⇒ 两条编译命令同属一个 target ✓ ⇒
      构建 `page_slot_test` = 先编 page_slot.cpp、再编 a2.cpp ⇒ **产物被后者覆盖** ✗；
      而 `a2_test` 自身失去配方 ✗ ⇒ 二进制早已存在 ⇒ make 不重建 ⇒ check 一直绿 ✓。
⇒ ⇒ 后果：`check` **从未运行过我的测试** ✗ ⇒ 第 15 轮的造错当然不会变红 ✓（矛盾解释完毕 ✓）。
```

## 二、修复（✓）

```make
# 修法：把 a2 的目标行与配方还原为**独立规则** ✓（新规则保持独立 ✓）：
$(BUILD_DIR)/page_slot_test: src/tests/page_slot_test.cpp src/backend/cve_2026_43499/memory/page_slot.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/page_slot_test.cpp -o $@
$(BUILD_DIR)/a2_test: src/tests/a2_test.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/a2_test.cpp -o $@
```

## 三、两层验证（✓✓）

```
① 身份核验：修后 `./build/host/page_slot_test` 打出 `page slot: … / page slot: PASS` ✓（不再是 a2 的输出 ✓）；
② §8 证伪：反置"二次 stash 应被拒" ⇒ **`check` RC=2** ✓ + `page slot: FAIL a second stash is refused` ✓
   ⇒ **守卫确实能失败** ✓；撤回（`造错` 计数 = 0 ✓）后按守则 §8「源码回位 ≠ 产物回位 ✓」先
   `rm -f build/host/page_slot_test` 再跑 ⇒ **109 PASS** ✓。
```

## 四、门禁（✓，全绿 ✓）

```
check → RC=0，**109 PASS** ✓（`page slot: PASS` 逐字出现 ✓）；
android → RC=0，0 error / 0 warning ✓；lint → RC=0 ✓。
```

## 五、教训（✓，登记 ✓）

```
· 在 Makefile 里插入规则时：**新规则的末尾必须显式收尾** —— 不能依靠空行 ✓；
  断言"能失败"必须同时确认"被运行的是谁"（本次用**跑一次二进制看输出**当场识破 ✓）；
· 守则 §8 的两条都用到：造错证伪 ✓ + `rm -f` 复核产物身份 ✓。
```
