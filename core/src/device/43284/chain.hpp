#pragma once
/** @file device/43284/chain.hpp
 *  本构建能提供的 43284 缝（数据性的那些：两张图与钩子计划）。
 */
#include "boundary/jni/api_43284.hpp"

#include "backend/cve_2026_43284/access_params.hpp"




namespace ghostlock::jni {
    /**
     *  会话参数 → 门的材料：spi / 端口 / ICV / 两个 32 字节密钥。
     *
     *  密钥长度不对就没有材料（set = false）—— 零密钥不是凭据，门的开启会据此拒绝，
     *  而不是拿一段零去连。
     */
    /** @return ESP socket fd（发送端绑 sender_port、连 encap_port）；失败 -1。 */
    [[nodiscard]] int device_connect_esp(const Init43284Values &params) noexcept;
    /** @return 关掉一个 socket；fd < 0 则为 false。 */
    [[nodiscard]] bool device_close_socket(int fd) noexcept;

    /** @return 建一个 Access（借用落地对象）；失败 0。 */
    [[nodiscard]] std::uint64_t device_make_access(std::uint64_t landingId) noexcept;
    /** @return 写钩子（站点一个字 + landing 载荷）。 */
    [[nodiscard]] bool device_write_hook(const contract::WriteHook43284Values &values,
                                        const contract::AccessParams43284Values &params_values,
                                        const Init43284Values &sa_source) noexcept;
    /**
     *  恢复钩子：把 plan 的两个字写回站点所在的块并回读（steps::restore_hook 的那一步）。
     *
     *  只搬运、不判断：面由 access 票据给（同一 access ⇒ 同一拼接面与同一通道），宽度与 plan 由
     *  调用方给；是否幂等、先改哪个字由 steps 层决定（见 pagecache_steps.hpp 的 restore_hook）。
     *
     *  @param values       写面：access 票据与块宽（与 writeHook 同一张表）
     *  @param params_values 同一份 AccessParams（路径借用式，只在调用期有效）
     *  @param plan_values  plan（与 hookPlan 同源的那张，恢复要按它把 guard 与 displaced 写回）
     *  @param sa_source   会话材料（access 的读/写要 usable_for_access，必须有 sa）
     *  @param why         失败时收具名原因（steps 层的名字原样带出）
     *  @return 块写回并回读一致 true；否则 false（上层给 hook-restore:...）
     */
    [[nodiscard]] bool device_restore_hook(const contract::WriteHook43284Values &values,
                                           const contract::AccessParams43284Values &params_values,
                                           const contract::HookPlan43284Values &plan_values,
                                           const Init43284Values &sa_source,
                                           const char **why) noexcept;
    /**
     *  一次镜像写的真实账：与 native 的 `write outcome:` 行同源的五个数。
     *
     *  @note 语义与 steps::WriteOutcome（core/src/backend/cve_2026_43284/steps/pagecache_steps.hpp:107-113）
     *        逐项相同：written = 写调用报成功（整块）的块数，verified = 真回读且比中的块数，
     *        rolled_back = 回滚写回的旧块条数；两个标志只在提供了 journal 时才可能非零。
     *        早退（没有句柄 / 参数不可用）时五个数全保持 0 —— 没有发生的事不猜。
     */
    struct WriteOutcome43284 final {
        std::uint64_t written = 0U;
        std::uint64_t verified = 0U;
        std::uint64_t rolled_back = 0U;
        bool journal_overflow = false;
        bool rollback_incomplete = false;
    };

    /**
     *  写一个镜像并逐块校验（patch#1 用的那一步）。
     *
     *  @param values        写面：accessId / offset / width / bytes / budget，全由 Kotlin 给。
     *  @param params_values 同一份 AccessParams（路径借用式，只在调用期有效）。
     *  @param sa_source     会话材料（usable_for_access 要求 sa 已设置）。
     *  @param why           失败时收具名原因（steps 层的名字原样带出）。
     *  @param outcome       可选的本次账（见 WriteOutcome43284）；null ⇒ 不写。
     *  @return 全部块写完并校验通过 true；否则 false（上层给 image-write-refused）。
     */
    [[nodiscard]] bool device_write_image(const contract::WriteImage43284Values &values,
                                         const contract::AccessParams43284Values &params_values,
                                         const Init43284Values &sa_source,
                                         const char **why,
                                         WriteOutcome43284 *outcome = nullptr) noexcept;
    /** @return 初始化 Access（路径字段借用式，必须活到调用结束）。 */
    /** @return 0 成功；否则是 Access 的状态码（上层翻成具名拒绝）。 */
    [[nodiscard]] int device_access_initialize(std::uint64_t accessId,
                                              const contract::AccessParams43284Values &values,
                                              const Init43284Values &sa_source) noexcept;
    /** @return 拆掉一个 Access。 */
    [[nodiscard]] bool device_destroy_access(std::uint64_t accessId) noexcept;

    /** @return 往落地对象写一块（十六字节）。 */
    [[nodiscard]] bool device_land_block(std::uint64_t landingId, std::uint64_t offset,
                                         const std::uint8_t *block) noexcept;
    /** @return 从落地对象读回一块（十六字节）。 */
    [[nodiscard]] bool device_read_landing_block(std::uint64_t landingId, std::uint64_t offset,
                                                 std::uint8_t *out) noexcept;

    /** @return 用会话参数的材料把落地对象拉起来。 */
    [[nodiscard]] bool device_bring_up(std::uint64_t landingId, const Init43284Values &params) noexcept;
    /** @return 窗口的下一个序号；id 无效 ⇒ 0。 */
    [[nodiscard]] std::uint64_t device_next_sequence(std::uint64_t landingId) noexcept;
    /** @return 让落地对象采用下一个序号。 */
    [[nodiscard]] bool device_adopt_sequence(std::uint64_t landingId, std::uint64_t next) noexcept;

    /** @return 建 socket 并存表；失败 0。 */
    [[nodiscard]] std::uint64_t device_esp_socket_id(const Init43284Values &params, int &why) noexcept;
    [[nodiscard]] std::uint64_t device_esp_socket_id(const Init43284Values &params) noexcept;
    /** @return 按 id 关掉 socket。 */
    [[nodiscard]] bool device_close_socket_id(std::uint64_t socketId) noexcept;
    /** @return 建拼接面（薄门 + 真拼接面）；失败 0。 */
    [[nodiscard]] std::uint64_t device_make_surface(std::uint64_t targetId, std::uint64_t socketId) noexcept;
    /** @return 拆掉拼接面（连它的落地对象一起）。 */
    [[nodiscard]] bool device_destroy_surface(std::uint64_t surfaceId) noexcept;
    /** @return 按拼接面建落地对象；失败 0。 */
    [[nodiscard]] std::uint64_t device_make_landing(std::uint64_t surfaceId) noexcept;
    /** @return 拆掉落地对象。 */
    [[nodiscard]] bool device_destroy_landing(std::uint64_t landingId) noexcept;

    /** @return 关掉一扇门；id 无效或门本来没开则为 false。 */
    [[nodiscard]] bool device_close_target(std::uint64_t targetId) noexcept;

    [[nodiscard]] ghostlock::backend::cve_2026_43284::SaMaterial sa_material_of(
        const Init43284Values &params) noexcept;

    /** @return 本构建的 43284 缝；其余（门、块读写、模块校验、触发、等待）留空 ⇒ op-not-wired。 */
    [[nodiscard]] DeviceOps43284 device_ops_43284() noexcept;
}  // namespace ghostlock::jni
