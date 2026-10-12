# 43499 · 第 57 轮：判据字段对齐，但**少了一个关键读数 `clean=`** ✓✓

## 一、字段对齐（✓）

```
退役 `route_status.h`：`RouteStatus{ code, step, error_number, userspace_clean, kernel_disarmed }`
                  + `is_clean() = userspace_clean && kernel_disarmed` ✓ / `is_dirty()` ✓；
本树 `route_status.hpp:42-47` **同名同义** ✓，且都已设过 1（`:267/:268` ✓ 与 `:384` ✓；退役多处 ✓）
⇒ ⇒ 「`kernel_disarmed` 缺失」这条**排除** ✗ ✓。
```

## 二、真正的缺口：读数 ✗✓（本轮最有价值的一条 ✓）

```
归档成功那次（真机逐字 ✓）：`[route] route_done status=0 **clean=1/1** step=0 errno=0 calls=1 success=1` ✓；
本树真机读数（我的打印 ✓）：`main: route status=0/0 attempts=16 calls=1 successes=1 stamp=…` ✗
⇒ ⇒ **没有 `clean=` 这一列** ✗ ⇒ 也就是"用户态是否干净 + 内核是否已解除武装"这个**判据读数**，
   档案里有、**我树不打印** ✗ ⇒ ⇒ 于是"写是否发生"这件事，我一直在用**别的量**（successes/stamp ✓）间接猜 ✓。
```

## 三、下一步（✓，一行改动 ✓）

```
在既有路由打印里加一列：`clean=%d/%d` = (`status.userspace_clean`, `status.kernel_disarmed`) ✓；
⇒ 一次真机跑即可读出我树的 `clean` 是 **1/1** ✓（= 与归档同 ✓）还是 **0/x** ✗（⇒ 那就直接解释了"写未发生"✓）。
判据：任一轮 `main: victim uid=0` ✓；辅助：`clean=1/1` ✓ 且 `step=0` ✓。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
