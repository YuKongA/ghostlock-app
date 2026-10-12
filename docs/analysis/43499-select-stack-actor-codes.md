# 43499 · `select_stack`：演员返回码已进报告（round 192）✓

## 落地（把丢掉的错误码捡回来 ✓）

```
SelectStackReport += waiter_lock / waiter_park / consumer_setattr   （round 192 ✓）
waiter   ：waiter_lock_ ← lock_pi(chain) 的返回 ✓；waiter_park_ ← wait_requeue_pi_until 的返回 ✓
consumer ：consumer_setattr_ ← setattr 的返回 ✓
收尾时一并写进报告 ✓；main 的打印加三个字段 ✓：
  main: select route status=… step=… attempts=… calls=… successes=… lock=… park=… setattr=… refusal=…
```

## 真机（本批未取到读数 ✗，如实 ✓）

```
3 跑全部撞奇偶门（payload-layout-not-accepted ✗）⇒ 无一到达 route ✓；
⇒ 下一批 4-6 跑即可取到 `lock=/park=/setattr=` 三个码 ✓（它们会把"没跑到 / 被拒 / 时刻不对"分开 ✓）。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```

## 读数预期（下一批判据 ✓）

| 读数 | 含义 | 下一步 |
| --- | --- | --- |
| `lock` ≠ 0 | waiter 连链都没拿到 ✗ | 查 waiter 线程是否起、chain 字状态 ✓ |
| `lock` = 0 且 `park` ≠ 0（如 -110 超时 ✗） | 拿到了链但没等到被搬 ✓ | 查 requeue 的时机与 pair ✓ |
| `park` = 0 且 `setattr` ≠ 0 | park 成功但消费者动作被拒 ✓ | 查 nice 轮转/策略 ✓ |
| `successes` ≥ 1 ✓ | 内核搬动了 ✓ | 看 `enforce` 1→0 ✓ |
