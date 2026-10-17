#pragma once
/** @file backend/cve_2026_43284/steps/pagecache_step_runner.hpp
 *  执行体的接线面：一次镜像写的 journal 缓冲、它的账，以及「缓冲由调用方给」这条边界。
 *
 *  为什么单独一个头：run_pagecache_step 的实现要拉门、拼接面与 access（见同名 .cpp），而
 *  「缓冲多大、记了几条、拒绝串怎么缀」这件事**不依赖设备** —— 执行体、JNI 与单测用的是同一份
 *  规则，所以从那里拆出来。
 *
 *  边界（与 pagecache_steps.hpp 一致）：
 *    - 本单元不分配全局、不持有存储：缓冲是调用方的局部 vector，一块一条记录、一条一个块宽；
 *    - 本单元不改攻击原语与页来源：写的还是同一个 access / channel / 宽度；
 *    - 失败一律具名；回滚有块写不回时，拒绝串末尾带具名后缀 c :rollback-incomplete。
 */
#include <cstddef>
#include <span>

#include "backend/cve_2026_43284/steps/pagecache_steps.hpp"

namespace ghostlock::backend::cve_2026_43284::steps {
    /**
     *  一段镜像按 width 切成多少块（width 为 0 ⇒ 0 块）。
     *
     *  与 write_image_verified 的块数同一算式：最后一块不足 width 也占一块（补零）。
     *  @param image 整段镜像
     *  @param width 一块的字节数
     *  @return 块数；width 为 0 时 0
     */
    [[nodiscard]] std::size_t image_blocks(std::span<const std::byte> image, std::size_t width) noexcept;

    /**
     *  journal 缓冲该多大：blocks * width（按块数算，不是按镜像字节数）。
     *
     *  一块一条记录，一条记录 width 字节；最后一块补零后同样占一条。
     *  @param image 整段镜像
     *  @param width 一块的字节数
     *  @return 字节数；width 为 0 时 0
     */
    [[nodiscard]] std::size_t journal_bytes_for(std::span<const std::byte> image,
                                                std::size_t width) noexcept;

    /** 一次 journaled 镜像写的账：WriteOutcome 之外，另带 journal 实际记下的条数与容量（都以块计）。 */
    struct JournaledWrite final {
        /** 整段是否写完并验过（与 write_image_verified 的返回值是同一件事）。 */
        bool ok = false;
        /** 写了几块、验了几块、回滚了几块，以及两个回滚标志。 */
        WriteOutcome outcome{};
        /** 这一段总共几块。 */
        std::size_t blocks = 0U;
        /** journal 实际记下的条数（失败时到第一块不通过的那一块为止）。 */
        std::size_t entries = 0U;
        /** 这块缓冲能记几条（= 字节数 / 块宽）。 */
        std::size_t capacity_blocks = 0U;
    };

    /**
     *  把调用方缓冲挂到 request 上，跑一次镜像写，**成功与失败都打一行**账：
     *  c journal: n=<条数> overflow=<0|1> rolled=<n> incomplete=<0|1>。
     *
     *  缓冲由调用方持有（局部 vector，长度按 journal_bytes_for 算），本函数只借用它的 span：
     *  容量 = span 的字节数 / 块宽，并原样写进 c request.journal_capacity_bytes。
     *  失败且回滚有块写不回（c rollback_incomplete）时，拒绝串末尾缀 c :rollback-incomplete
     *  （写进 @p decorated_refusal 并把指针指回它），让上层只看拒绝串就能判。
     *
     *  @param request 已填 access / params / image / offset / width / verify_every 的请求
     *  @param journal 调用方的缓冲（span 为空 ⇒ 不记日志、不回滚，行为与加日志之前一致）
     *  @param refusal 收具名拒绝（必须非空；成功时由 write_image_verified 保持不变）
     *  @param decorated_refusal 缀过 rollback-incomplete 的拒绝串落点；空 span ⇒ 拒绝串不缀
     *  @return 账（含 n 与容量）；其 ok 与 write_image_verified 的返回值一致
     *  @note 条数按「本段尝试过的块」与容量取小：写之前每块记一条，所以失败时是到失败块为止；
     *        唯一的例外是 c step-journal-unread —— 那一块的旧块没读到，因此它没有进 journal。
     */
    [[nodiscard]] JournaledWrite write_image_journaled(WriteRequest &request, std::span<std::byte> journal,
                                                       const char **refusal,
                                                       std::span<char> decorated_refusal) noexcept;
}  // namespace ghostlock::backend::cve_2026_43284::steps
