# 43499 · 探针清理与干净基线（round 109）

## 清理（临时物零容忍 ✓；只用精确字面替换，不做按行删 ✓）

```
清除项（全部登记在案）：
  multicast_route.hpp : TEMPWAIT / TEMPREQ(before,after) / TEMPTRIG / TEMPRETRY×4 / TEMPSTAMP /
                        TEMPCREATE×3 / TEMPSTART×2                ⇒ 0
  steps/words_region   : TEMPPARF / TEMPPAR / TEMPMAP           ⇒ 0
  main.cpp             : TEMPPAGE / TEMPTASK / TEMPBISECT（含读回块）/ TEMP-VERIFY 注释×2 ⇒ 0
同时：写目标从二分实验的自家缓冲 +0x2000 换回真正的受害者 cred 字段；
      shaping 两端恢复为发完即关。
```

## 干净基线（真机逐字）

```
main: route status=0/0 attempts=15 calls=1 successes=1 stamp=-1/99
main: step result w1 => won / w2 => won / w3 => won
main: victim comm after len=15      ← 旁证：写未落
main: victim uid=2000               ← 主判据：未 root
```

⇒ 去掉全部探针之后，链的表现与探针时期**完全一致** ⇒ 说明探针没有影响机制，
  那些结论也不是探针造成的。

## 门禁

- android → RC=0，0 error / 0 warning；check → 108 PASS；lint → 0。
