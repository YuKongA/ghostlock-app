# 43499 · 第 74 轮：退役路线生命周期 35 行 ⇒ 暴露第二个读数缺口（`disarm`）✓

## 一、退役契约（✓，逐字 ✓）

```cpp
// route_lifecycle.hpp（35 行）
template<class R> concept RouteLifecycle = requires(R &route) {
    { route.prepare() } -> std::convertible_to<int32_t>;   // ★ 与 0 比较 ✓
    { route.execute() } -> std::same_as<RouteStatus>;
    route.disarm();    route.destroy();
    { route.status } -> std::convertible_to<RouteStatus>;
};
template<RouteLifecycle R> RouteStatus run_route_lifecycle(R &route) {
    if (route.prepare() == 0) { (void) route.execute(); }   // ★ prepare==0 才 execute ✓
    route.disarm();  route.destroy();  return route.status;
}
⇒ 契约顺序 = **prepare → execute → disarm → destroy** ✓（注释还写明"无 vtable/间接派发" ✓）。
```

## 二、对照本树（✓）

```
本树等价：`prepare` ✓ → `run_actors` ✓ → `execute` ✓ → `stop_actors` ✓（第 23 轮已核对结构 ✓）；
⇒ 且 `execute` 显然跑到了 ✓（⇒ 有 `route status=…` 读数 ✓）⇒ 说明 `prepare` 返回 0 ✓（⇒ 强于本树一层的确认 ✓）。
```

## 三、暴露的第二个读数缺口（✗✓，与 `clean=` 同类 ✓）

```
归档日志有：`[*] mcast ghost **disarm ret=-1 errno=110**` ✓（⇒ 退役**打印了 disarm 的结果** ✓）；
本树：没有该读数 ✗ ⇒ 于是"解除武装"这一步的结果**从未被观测** ✓；
⇒ ⇒ **⇒ 修法一行**（与第 58 轮补 `clean=` 同形 ✓）：把 `disarm`/`stop_actors` 的返回与 errno 打出来 ✓；
⇒ 判据：与归档同形（`disarm ret=-1 errno=110` ✓）⇒ ⇒ 若**不同** ✗ ⇒ 即命中差异 ✓✓。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
