# 43499 写落地 · 别名空间的错（round 70，2026-10-11 清晨）

## 对标（归档成功 vs 我）

```
归档： W2: cred === target=0xffffff898138e198 mode=2 leaf=0
      ⇒ mode=Credential ✓、leaf=0（= preserve_child ✓）与我一致 ✓
我　： main: layout matches=1 accepts=1 value=0xffff7f8fe3760588 mode=2
夹具： init_cred = 0x0000000002bfd588 ；ref0..3_image = 0xffffffc00ab23a80 / 0xffffffc00acce110 / …
```

## 我的错（具名）

- 我把 `init_cred`（**镜像地址**，`0x2bfd588` 量级）加上了 `alias_delta`（由**堆**的 snitch 泄漏量出，
  `0xffff7f8f…` 量级）⇒ 结果落在**堆映射空间** ✗；
- 退役那套的凭据别名在 **`0xffffffc0…`（镜像 → 直接映射）** ✓ ⇒ 这是**另一个别名空间** ⇒ 两者不可相加 ✗；
- 结论：`credential_value` 必须走 **镜像→直接映射**的那条公式（`layout_from_document.hpp:43-59` 的 `offset_to_direct` ✓，
  其 `p0_page_offset` 就是 `0xffffffc0…` 那个基 ✓），**不能**用堆的 `alias_delta` ✗。

## 下一轮（一次修完，判据仍是 uid=0）

1. 从夹具/文档取 `p0_page_offset` 等价物（夹具里有 `slide_boot_id`/`slide_loggers_0_1`/`slide_nfulnl_logger` ✓，
   它们正是这套"锚点+slide"的量 ✓；`abi.offset` 段也已有 `slide_*` ✓）；
2. 按 `offset_to_direct` 的公式算 `credential_value`（`image_text_base`/`phys_*` 为 null 时就退化为 `p0 | offset` ✓）；
3. 页里 `cred_copy` 槽的镜像地址（`credential_fops` ✓）同样要落在同一空间 ✓ —— 一并核；
4. 跑 ⇒ `victim uid=0`。

## 顺带记录（供明天）

- 凭据路径已整条接线（十五键读取 ✓、`build_credential_image` ✓、`mode=Credential` ✓、`target=child_task+cred_off` ✓），
  真机上**无任何 credential-* 具名拒绝** ✓ ⇒ 接线这一层已经是通的 ✓；
- 剩下的全部问题收敛成一句话：**把凭据别名放回正确的别名空间** ✓。
