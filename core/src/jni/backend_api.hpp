#pragma once
/** @file jni/backend_api.hpp
 *  每个 backend 一个 JNI 库的共用词汇（用户裁定 2026-10-14）。
 *
 *  边界不再是"一个 op 名 + 一帧字节"：每个 backend 有自己的库、自己的具名函数，参数是类型化的
 *  数字与字符串，结果是类型化的数字/数组/字节数组。这里只放两样东西：
 *   1. 调用码：0 = 成功，负数是具名失败；
 *   2. 具名拒绝缓冲：最后一次失败的**名字**（例如 op-not-wired:write_word），由函数旁边的
 *      refusal(handle) 取出 —— 名字要能被人直接读懂，不能被压成一个数字。
 *
 *  另外提供字段布局的自描述：数组参数/结果的顺序由 native 用字符串表导出，Kotlin 侧对拍，
 *  于是"第几个是哪个字段"这件事不会在两端各写一份。
 */
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace ghostlock::jni {
    /** 一次调用的结局。0 成功；负数都是具名失败。 */
    enum class CallCode : int {
        Ok = 0,
        /** 前置条件未成立（例如还没有准备/发现）：可以稍后再试。 */
        NotReady = -1,
        /** 参数不合法：调用方的问题，重试无用。 */
        BadParameters = -2,
        /** 这个 build 不提供这个能力（例如路由不能读）：重试无用。 */
        Unsupported = -3,
        /** 设备动作失败：原因看 refusal。 */
        Failed = -4,
        /** 句柄不存在或已回收：调用方拿错句柄了。 */
        StaleHandle = -5,
        /** 这个会话上一次调用还没结束：调用方并发用了同一个句柄。 */
        Busy = -6,
        /** 手上这个目标不是本会话解析出来的（或已被替换）：不许当成解析结果用。 */
        StaleTarget = -7,
        /** 还在进行中：不是失败，也不是成功，稍后可以再问。 */
        InFlight = -8,
        /** 会话已经关掉：除 snapshot / shutdown / destroy 之外一律拒绝（重试无用）。 */
        Closed = -9,
    };

    /** 调用码的名字（日志与自检用）。 */
    [[nodiscard]] inline const char *call_code_name(CallCode code) noexcept {
        switch (code) {
            case CallCode::Ok: return "ok";
            case CallCode::NotReady: return "not-ready";
            case CallCode::BadParameters: return "bad-parameters";
            case CallCode::Unsupported: return "unsupported";
            case CallCode::Failed: return "failed";
            case CallCode::StaleHandle: return "stale-handle";
            case CallCode::Busy: return "busy";
            case CallCode::StaleTarget: return "stale-target";
            case CallCode::InFlight: return "inflight";
            case CallCode::Closed: return "closed";
        }
        return "unknown";
    }

    /** 一条具名拒绝。定长、可拷贝、永远是 C 字符串（调用方拿到的是 const char *）。 */
    class Refusal final {
      public:
        /** 记下一条拒绝（超出长度的部分被截断，不截断成半个多字节字符也没关系：名字都是 ASCII）。 */
        void set(std::string_view text) noexcept {
            const std::size_t bytes = text.size() < sizeof(text_) - 1U ? text.size() : sizeof(text_) - 1U;
            std::memcpy(text_, text.data(), bytes);
            text_[bytes] = '\0';
        }

        void clear() noexcept { text_[0] = '\0'; }

        [[nodiscard]] const char *c_str() const noexcept { return text_; }

        [[nodiscard]] bool empty() const noexcept { return text_[0] == '\0'; }

      private:
        char text_[128] = {};
    };

    /** 记下具名拒绝并回答调用码：所有失败路径都走它，避免每处各写一遍。 */
    [[nodiscard]] inline CallCode refuse(Refusal &refusal, const CallCode code,
                                         const std::string_view text) noexcept {
        refusal.set(text);
        return code;
    }

    /** 参数/结果里"第几个是什么"的自描述条目。 */
    struct FieldName final {
        const char *name;
    };

    /**
     * 一个 backend 的函数面：每个函数一个具名调用，参数类型化。
     *
     * 它不是接口类（没有虚函数）：两个 backend 的库各自实现自己的那组函数，Kotlin 侧也各自声明，
     * 所以"这个 backend 有哪些调用"在这两个文件里一眼看得完。
     */
    namespace api {
        /** 会话相位（与 native 状态机一致）。 */
        enum class Phase : std::uint64_t {
            Declared = 0,
            Ready = 1,
            Failed = 2,
            Closed = 3,
        };
    }  // namespace api
}  // namespace ghostlock::jni
