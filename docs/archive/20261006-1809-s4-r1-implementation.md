> 归档说明（2026-10-06）：归档原因 = **已实行**（implementation 记录，按 docs/archive/README.md 十 政策）。证据：① 勾选项 **不适用**（该件无勾选框，done=0/open=0）② 点名提交 **不适用**（该件无提交号）③ **产物核验通过** —— 路径型产物在（见各件正文文件名）+ 裸名归一化后命中（lkm_image.cpp / execution_binding.cpp / backend_terminal.cpp / lkm_policy.cpp / profile_registry_test.cpp 均在 src 或 app 下 find 到）。原始路径 = docs/analysis/s4-r1-implementation.md；归档路径 = ��

# S4 · R1 实施设计：schema 权威 + SchemaRegistry + bind_all（native 内化，**不动 wire**）

> 上游权威：`config-wire-redesign-plan.md`（R1、§4.2 FieldSpec、§5.4 profile）。本文件只做 R1 的可执行细化。
> 自上而下顺序：**R1（本文件）→ R2（wire owner-qualified）→ R2b（CLI）→ R3（HOCON）→ R4（43284 policy）→ R5（清理）**。

## 1. 目标 / 非目标

**目标**
1. `FieldSpec` 扩为完整声明：`path`（先沿用现 section/key，R2 才改 owner-qualified）、`wire`（uint/int/bool/string 预留）、`required`、
   `default`（字面量 / derived 回调 / convention 路径）、`source`、`doc`；
2. 新增 `SchemaRegistry`：按 `ComponentSelection` 组合 `platform::abi` + `backend::<id>`（+ 后续 `platform::vivo`）；
3. 新增 `bind_all(registry, document, views..., mode)`：**一趟**完成 required 校验（错误带 path）+ 默认值落库 + 诊断 `default_used=<path>`；
4. backend 不再自判 required/默认：把现散落逻辑上收（见 §3）。

**非目标（R1 明确不做）**
- 不改 wire：`schema == 3`、段名/键名/字节全部不动（owner-qualified 是 R2）；
- 不新增 `string` 的实际使用（R2/R4 才用）；
- 不动 HOCON（R3）、不动 CLI（R2b）、不动 route 归属（R2）。

## 2. 现状（取证位置）

| 现状 | 位置 |
|---|---|
| `FieldSpec` 只有 `{section,key,width,required,optional?,setter}` | `src/core/profile/schema.hpp` |
| `bind<Schema>` / `bind_all<Schemas…>` 已存在，但无 default/derived/convention | `src/core/profile/schema.hpp` |
| `platform::abi::Schema`（Sections → kernel 结构） | `src/core/platform/abi.hpp` |
| `Cve2026_43499Schema`（`backend.cve_2026_43499.steps` + meta） | `src/core/backend/cve_2026_43499/schema.hpp` |
| `Cve2026_43284Schema`（7 个可选字段） | `src/core/backend/cve_2026_43284/schema.hpp` |
| **43284 硬 required 检查 + selinux ctx 默认** | `backend/cve_2026_43284/backend_terminal.cpp`（`ProfileIncomplete`；`kSelinuxExecContextVendorModprobe` 默认） |
| **KMI 由 release 推导** | `backend/cve_2026_43284/lkm/lkm_policy.cpp`（`kernel_major*1000+minor`） |
| **root package / late-load 参数约定** | `backend/cve_2026_43284/lkm/lkm_image.cpp`（`default_root_package`、`build_late_load_command`） |
| **carrier 探测回退首个默认** | `backend/cve_2026_43284/execution_binding.cpp` |

## 3. 逐文件清单（native only）

1. `src/core/profile/schema.hpp`：扩 `FieldSpec`（+`required`/`default`/`source`/`doc`/`wire` 预留）；新增
   `DefaultValue{None | Literal(u64) | Derived(fn) | Convention(path)}`；`bind_all(registry, doc, sink, mode)`；
   required 缺失 → `BindStatus::Rejected` 且错误串带 **owner-qualified path**；默认触发 → 诊断 `default_used=<path>`；
2. `src/core/profile/registry.hpp`（新）：`SchemaRegistry`（按 backend/terminal 选择 owner schema 集合）；`mode = Production | Test`；
3. `src/core/platform/abi.hpp`：把 ABI 字段标 `required`（缺省字段保持可选）、补 `doc`；
4. `src/core/backend/cve_2026_43499/schema.hpp`：`steps` 标 required（或 default），补 `doc`；
5. `src/core/backend/cve_2026_43284/schema.hpp`：7 字段补 `doc` + `default`（`selinux_exec_context` → 字面量默认；`kmi` → **derived(release)**；
   `lkm_path` → **convention($GHOSTLOCK_HOME/helper.ko)**；`late_load_args`→字面量默认）——**取值必须与现状逐字节一致**；
6. `backend/cve_2026_43284/backend_terminal.cpp`：删除自判 `ProfileIncomplete`/默认赋值，改为消费 bind_all 结果 + 上报 `default_used`；
7. `backend/cve_2026_43284/lkm/lkm_policy.cpp`：保留 `resolve_lkm_selection` 的策略判定，但 `kmi`/`lkm_path`/`late_load_args` 的**取值来源**改由 schema 提供；
8. `backend/cve_2026_43284/execution_binding.cpp`：carrier 回退语义不变，但「第一默认」的声明进 schema（`convention`）；
9. 测试：`src/core/tests/`（新增 `profile_registry_test.cpp` + 扩展既有 schema/bind 测试）：required 缺失报 path、三种 default 触发并落 `default_used`、
   `mode=Test` 放宽、以及**默认值与现状等值**的对照断言；`src/Makefile` 接线。

## 4. 不变量（R1 验收硬条件）

- **wire 字节不变**：`profile_manifest_v3_test` / `profile_manifest_v3.tsv` 不变；`glkv3` 编码/解析零改动；
- **43499 行为不变**：真机 43499 门禁（冷启）PASS；二进制层面 `cmp_disasm` 可选诊断应无攻击函数差异；
- **43284 解析结果不变**：以设备上 App 产出的 43284 文档跑 staged 全链，最终 `lkm_path`/`kmi`/`selinux_exec_context`/`late_load_args`
  与 R1 前**逐字节一致**（在诊断里打印 resolved 值做对照）；
- 无新增可变全局；零告警；不违反 R1 include 防火墙。

## 5. 门禁

| 门槛 | 命令 |
|---|---|
| host | `make -C src native-host-tests`（含防火墙整行 + manifest 测试） |
| NDK | `ANDROID_NDK_HOME=… make -C src ghostlock -B`（零告警） |
| lint | `ANDROID_NDK_HOME=… make -C src lint-tidy`（0 findings） |
| 真机 43499 | 冷启 + adb 免 App（`--load-prebuilt-profile`）→ `child is root!` / KernelSU ready |
| 真机 43284 | staged 全链（adb 试验台）→ `run.cve_2026_43284 ok` + resolved 值与 R1 前一致 |

## 6. 风险

- 把默认值搬进 schema 时**极易改变有效取值**（如把「缺省→modprobe」写成字面量 0 以外的值）→ 用「R1 前后 resolved 值对照」兜底；
- `bind_all` 从「可选宽松」变「required 严格」可能让**既有 profile 突然被拒** → required 只对**真正必需**的字段开启，其余保持可选 + default；
- 测试面较大：需要覆盖 4 类 owner 的组合与 mode 语义。
