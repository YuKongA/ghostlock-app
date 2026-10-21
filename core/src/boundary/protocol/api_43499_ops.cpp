#include "device/43499/chain.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "boundary/jni/api_43499.hpp"
#include "contract/attack_contract_values.hpp"

/** @file boundary/protocol/api_43499_ops.cpp
 *  43499 device action table.
 *
 *  Production never built DeviceOps43499: `Backend43499` received an all-null table, so every op stopped at
 *  its null-slot guard and answered op-not-wired (device readings, 2026-10-15). This unit builds the table and
 *  wires the two slots whose primitives already exist in api_43499_device.cpp; the rest stay null so the
 *  caller names the refusal instead of failing silently.
 *
 *  The adapters only map shapes (params/request/facts <-> access id, route, offset, address) and turn the
 *  eight-byte word into a block; they decide nothing -- the choices live in the Kotlin assembly.
 */
namespace ghostlock::jni {
    namespace {
        /** Write one word: the value becomes an eight-byte little-endian block for the existing primitive. */
        bool land_43499(const SessionParams43499 &, const WordWrite43499Values &request,
                        Facts43499 &, void *context) noexcept {
            auto *const ctx = static_cast<Ops43499Context *>(context);
            if (ctx == nullptr) {
                return false;
            }
            std::vector<std::uint8_t> block(8U, 0U);
            for (std::size_t i = 0U; i < block.size(); ++i) {
                block[i] = static_cast<std::uint8_t>((request.value >> (8U * i)) & 0xFFU);
            }
            const FormMapping mapping = map_write_form(
                write_form_of(static_cast<std::int32_t>(request.form)));
            if (mapping.mode == 0) {
                return false;
            }
            const char *why = nullptr;
            const std::uint64_t offset = request.offsetPresent
                ? static_cast<std::uint64_t>(request.offset)
                : 0U;
            return device_write_43499(ctx->access_id, ctx->route, offset,
                                      static_cast<std::uint64_t>(request.address), block,
                                      mapping.mode, mapping.preserve, &why);
        }

        /** Read one word through the existing primitive. */
        bool read_43499(const SessionParams43499 &, std::uint64_t address,
                        std::uint64_t &value, void *context) noexcept {
            auto *const ctx = static_cast<Ops43499Context *>(context);
            if (ctx == nullptr) {
                return false;
            }
            std::vector<std::uint8_t> out{};
            const char *why = nullptr;
            if (!device_read_43499(ctx->access_id, ctx->route, 0U, address, 8U, out, &why) || out.size() != 8U) {
                return false;
            }
            value = 0U;
            for (std::size_t i = 0U; i < out.size(); ++i) {
                value |= static_cast<std::uint64_t>(out[i]) << (8U * i);
            }
            return true;
        }
    }  // namespace

    DeviceOps43499 device_ops_43499(Ops43499Context *context) noexcept {
        DeviceOps43499 ops{};
        ops.land = &land_43499;
        ops.read = &read_43499;
        ops.context = context;
        return ops;
    }
}  // namespace ghostlock::jni
