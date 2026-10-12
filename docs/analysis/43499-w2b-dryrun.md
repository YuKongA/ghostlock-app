# 43499 · W2b 干跑成功：本树能正确表达修复载荷（round 115）

## 真机逐字

```
main: repair target=0xffffff802abfd590  mode=1 preserve=0  pc=0xffffff802abfd588 right=0x0 left=0x0
                                                                 ↑ = target − 8 ✓（叶子臂 ✓）
```

## 对照退役（逐项 ✓）

| 项 | 退役 | 本树干跑 |
| --- | --- | --- |
| target | `data_alias(init_cred_image_addr()) + 8` ✓ | `0xffffff802abfd590` ✓（= 归档别名 + 8 ✓） |
| mode | `WriteMode::Zero` ✓ | `1`（Zero ✓） |
| leaf | `1` ⇒ `preserve_child = false` ✓ | `preserve=0` ✓ |
| 元组 | 叶子臂 `{pc = target−8, right=0, left=0}` ✓ | `pc=0xffffff802abfd588, right=0, left=0` ✓ |

⇒ **本树已能正确表达 W2b 的载荷** ✓ —— 剩下的只是"发送它 + 跑第二轮" ✓（前半段的发送/轮次与 W2 同形 ✓）。

## 剩下的半段（下一步 ✓，规格已定）

1. 第二份页面按上面的请求 build ✓（`build_payload_page` 用同一个 `page` 缓冲即可 ✓）；
2. **发送**：再开一对 shaping（或复用同一对再发一次 ✓）⇒ 第二份页面的内核地址由**第二次发送**决定 ✓；
   ⇒ 相应地 stamp 的 `fake_task`/`fake_lock` 也要指向**第二份页面** ✓（即需要第二份 route 配置 ✓）；
3. **第二轮**：用第二份配置再跑一次窗口（本树既可"两个 `MulticastRoute` 实例"✓，也可"重配一次"✓）；
4. 判据：`main: victim uid=0` ✓；旁白：第二轮 `status=0 clean=1/1 step=0` ✓ 与第一轮同形 ✓。

## 备注

- 本轮的打印行是**阶段性**的 ✓：等第二半段落地后它会并入真正的 W2b 流程 ✓（届时一并清理 ✓）。
