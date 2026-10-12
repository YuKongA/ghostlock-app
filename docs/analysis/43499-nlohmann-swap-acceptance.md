# 换库验收归档：自写 JSON 解析 ⇒ nlohmann/json（wire_doc）

> 范围：用户裁定「把自己写的 json 解析删了，用库」（2026-10-12）。
> 写范围：`core/src/script/wire_doc.cpp` ＋ `wire_doc.hpp` 私有面（其余文件未动）。
> 本页只记**可复核证据**与**未验证项**，不含推测。

## 1. 代码证据（四组）

```
① 体量：core/src/script/wire_doc.cpp 374 ⇒ **352** 行（wc -l）；wire_doc.hpp 95 ⇒ **106** 行。
   删除：手写 Cursor 全块（≈190 行：skip_ws/string/number/value/array/object/push_open/link/pop_open）
        ＋ 旧 parse() 体（19 行）＋ 仅属它的码 json-unterminated / json-bad-number。
② 解析入口：core/src/script/wire_doc.cpp:**195**
   `storage->document = nlohmann::json::parse(first, first + bytes.size());`
③ 自写解析器特征词：grep -cE 'Cursor|skip_ws|json-unterminated|json-bad-number' ⇒ **0** ✓
   （残留 2 处为 hex_digit：定义 :26 ＋ u64_of 使用 :305 ⇒ 属**类型化读**；删它会读坏 0x 十六进制 ⇒ 保留 ✓）
④ 契约面逐字保留：grep -cE 'WireDoc::(read_framed|root|child|element|edge_at|find|u64_of|bool_of|str_of|base64_decode)'
   ⇒ **10** 个定义 ✓（read_framed ＋ 9 个访问器）；上限常数未动：
   kMaxNodes 8192 / kMaxEdges 16384 / kMaxDepth 32 / kMaxPayloadBytes 1 MiB ✓
   具名码未动：json-bad-token / json-unexpected-end / json-too-large / json-too-deep / json-trailing
   （json-trailing 由**库的严格模式**保证 ⇒ 码保留、注释说明理由 ✓）
```

### 1.1 库错误 → 具名码映射（wire_doc.cpp:39 `refusal_of_parse_error`）

```
· 报文含 unexpected end of input ⇒ **json-unexpected-end**（截断）
· 报文含 expected end of input   ⇒ **json-trailing**（严格模式拒尾随）
· 其余                           ⇒ **json-bad-token**
实测（主机小程序，链接 wire_doc.cpp）：
  {"a":  ⇒ json-unexpected-end ✓      {  ⇒ json-unexpected-end ✓
  {"a": 1} x ⇒ json-trailing ✓         33 层 ⇒ json-too-deep ✓（31 层通过 ✓，上限 32 ✓）
```

### 1.2 换库带回的设备回归与修法（同批已修 ✓）

```
· 症状：真机 rc=**134**（SIGABRT），ghostlock.log **未生成** ⇒
  libc++abi: … invalid_iterator.207: cannot use key() for non-object iterators
· 根因：遍历容器时对**数组**迭代器调 `entry.key()` ⇒ nlohmann 抛 ⇒ `parse()` 是 `noexcept` ⇒ terminate
· 修法（两处）：① 遍历前先定容器种类（数组不取 key ✓）；② **整个 parse() 体**包进 try/catch，
  捕获 parse_error / bad_alloc / std::exception / ... ⇒ 一律转**既有具名码**（不新造码 ✓）
  ⇒ 教训写进 `parse()` 注释：「库会抛 ⇒ 全体包住 ⇒ 转具名拒绝」✓
```

## 2. 三线门禁

```
make -C core -j1 check   ⇒ EXIT=0   PASS=109   FAIL=0        （含 wire doc: PASS）
make -C core -B android NDK_TOOLCHAIN=$HOME/Library/Android/sdk/ndk/30.0.16248370/toolchains/llvm/prebuilt/darwin-x86_64/bin
                         ⇒ EXIT=0   error=0   warning=0     （产物 build/native/ghostlock 5567368 B）
（markdownlint 本轮**未跑** ⇒ 见 §4）
```

## 3. 真机读数（`adb -s QV770MFGJ1`；含一次 `adb reboot` 冷机复跑）

```
rc = **9**（修复前 134）                  ✓
main: refused (param-invalid) 计数 = **0** ✓
main: step result w1 => **won**           ✓（史上首次）
value=9（脚本的 surface.log 调用）= **1**  ✓
ghostlock.log 生成 ✓（首行 launcher -> logging up；[lua] 6 行 ✓）
```

## 4. 未验证项（照实列）

```
· 真机 **uid=0 未取得**：全流程走到 `main: refused (w2-not-rooted)`（victim uid=2000 ×16）
  ⇒ 换库**未**改变该结果（属另一条线 ⇒ docs/analysis/43499-w2-probe-blocked.md ✓）
· **窗口写是否落地**：未证（无「任意目标写入口」⇒ 探针线已归档收线 ✓）
· NDK 产物除**真机跑动**外未做其它运行验证；
· **markdownlint 未跑**（本轮未执行该门禁）；
· wire_doc.hpp 仅加**析构** ＋ 私有 `JsonStorage*`（106 行），公共面其余未动 ✓。
```
