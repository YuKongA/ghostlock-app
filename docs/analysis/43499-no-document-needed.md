# 更正：43499 并不需要 document（2026-10-15）

## 证据（三处，都指向同一结论）

1. `access_params.hpp:101-108` 有**开发者的自述注释**：

   > //TODO(access-assembly): the document requirement was tried here and reverted the same day …
   > the contract's tests declare a route and nothing else. The document is checked where the
   > document carriers are … at which point this door may demand one again.

   紧跟其后的那一行判断是**注释掉的**：

   ```cpp
   // return route != profile::RouteId::Unset && document != nullptr;
   ```

   ⇒ 也就是说 `usable_for_initialize()` **现在不要求 document** ✓

2. `params->document` 在 43499 的 .cpp 里**没有任何读取点** ✓（grep 无命中）；
3. `layout_from_document` 在本仓**没有外部调用者** ✓（除它自己的头文件外 grep 无命中）——
   它是为「文档搬运」保留的单元，不是链路上必经的一环。

## 因此我之前说「43499 硬依赖解析好的 profile 文档」是**说过头了** ✗

正确的是：

- document 字段仍在结构里，但**不被要求、也无人读**；
- 因此 43499 现在就能按「只传参」推进：**不需要那份文档** ✓；
- 我上一轮加的 `Access43499Abi`（10 个数字）也**不是阻塞项的前提**，而是「将来若要喂 layout 就用它」的准备 ✓；
- `run_pipeline_temp.cpp` 里是**临时流水线**自己解析文档字节（第 345/432 行），与参数结构里的 document 字段无关 ✓。

## 下一步（按此更正调整）

1. **直接接 43499 的读写原语**（`initialize` / `write` / `read` / `shutdown`），参数就用现有标量字段 + 我加的 10 个数字；
2. `document` 字段与 `layout_from_document` 的处置留到真实 profile 设计定案时再决定（那时要么喂数字、要么删字段 ✓）；
3. 真机跑一条最小序列 —— 43499 **不需要 ESP**，所以这一步不依赖任何外部材料 ✓。
