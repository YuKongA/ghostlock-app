/** @file contract/attack_contract_reader.cpp
 *  Android 构建锚点：强制每个 backend 库编译 D1 生成读取器。
 */
#include "contract/attack_contract_reader.hpp"
#include "contract/attack_contract_model.hpp"

namespace ghostlock::jni::contract {
    /** @return D1 读取器编译时见到的字段总数。 */
    [[nodiscard]] std::size_t attack_contract_reader_field_count() noexcept {
        return kFieldCount;
    }
}  // namespace ghostlock::jni::contract
