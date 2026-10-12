# 43499 · 归档又暴露第三条结构差异：quarantine（round 119）

## 退役代码

```cpp
退役 spray.cpp:231  int32_t quarantine_reclaim_sockets()
退役 spray.cpp:236  void release_quarantined_reclaim_sockets()
```

⇒ 退役的 spray 生命周期是三段：**隔离 reclaim sockets → 用 → 到特定时点释放**；
  归档 W1 段的 private scratch repaired; releasing quarantine 正是"释放"那一步。

⇒ 本树是"每次准备各自开关" ⇒ 这是第三条结构性差异（W1b / quarantine / 已补的 W2b）。

## 关于 W1 落地观测的现实结论

- 归档用 SELinux permissive 证明 W1 写成了；
- 本机：/sys/fs/selinux/enforce 在启动时不可读（本树早已打印 enforce=unreadable）⇒ shell 身份大概率读不到；
- ⇒ 这条观测不可用（不硬凑）；想验证 W1 是否落地，只能做一件平时被 SELinux 拦下的事来对比（成本高），
  或接受无观测并如实记录。

## 下一步（可执行）

1. 补 W1b（与 W2b 同形：第二次准备 + 第二份页面 + 第二轮），目标取退役的 private scratch
   （需再读 w1 的修复目标怎么算）；
2. quarantine 三段生命周期暂不照搬，先记录；等 W1b/W2b 都稳再评估（避免一次动三处）。
