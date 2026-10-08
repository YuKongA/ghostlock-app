/* Host tests for B5-6 patch #1 and the crash_dump read bridge.
 *
 * The embedded splicehelper is real (generated from the upstream source); the
 * write surface and the bridge are fakes, so this test never opens, splices or
 * forks anything. It covers the padded plan, the offset-0 write and the
 * offset-16 verify, the failure branches and the vendor-path predicate. */

#include "backend/cve_2026_43284/steps/crash_dump.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
    using ghostlock::backend::cve_2026_43284::steps::CrashDumpBridgeOps;
    using ghostlock::backend::cve_2026_43284::steps::CrashDumpPatchError;
    using ghostlock::backend::cve_2026_43284::steps::CrashDumpPatchOps;
    using ghostlock::backend::cve_2026_43284::steps::is_vendor_path;
    using ghostlock::backend::cve_2026_43284::steps::kCrashDumpBlockBytes;
    using ghostlock::backend::cve_2026_43284::steps::kCrashDumpVerifyOffset;
    using ghostlock::backend::cve_2026_43284::steps::patch_crash_dump;
    using ghostlock::backend::cve_2026_43284::steps::real_crash_dump_bridge;
    using ghostlock::backend::cve_2026_43284::steps::splice_helper_plan;

    constexpr std::size_t kBufferBytes = 2048U;

    struct FakeCrashDump final {
        std::array<std::uint8_t, kBufferBytes> bytes{};
        std::size_t writes = 0U;
        bool fail_write = false;
        bool read_ok = true;
        bool corrupt_verify = false;
    };

    std::int32_t fake_write16(void *raw, std::uint64_t offset,
                              const void *bytes16) noexcept {
        auto *fake = static_cast<FakeCrashDump *>(raw);
        if (fake == nullptr || bytes16 == nullptr || offset + 16U > fake->bytes.size()) {
            return 1;
        }
        if (fake->fail_write) {
            return 2;
        }
        std::memcpy(fake->bytes.data() + offset, bytes16, 16U);
        ++fake->writes;
        return 0;
    }

    long fake_read16(void *raw, std::uint64_t offset,
                     std::uint8_t out[16]) noexcept {
        auto *fake = static_cast<FakeCrashDump *>(raw);
        if (fake == nullptr || out == nullptr) {
            return -1;
        }
        if (!fake->read_ok) {
            return 0;
        }
        if (offset + 16U > fake->bytes.size()) {
            return -1;
        }
        std::memcpy(out, fake->bytes.data() + offset, 16U);
        if (fake->corrupt_verify && offset == kCrashDumpVerifyOffset) {
            out[0] = static_cast<std::uint8_t>(out[0] ^ 0xFFU);
        }
        return 16;
    }

    long fake_bridge_read16(void * /*ctx*/, std::string_view /*path*/,
                            std::uint64_t /*offset*/,
                            std::uint8_t /*out*/[16]) noexcept {
        return 16;
    }

    long fake_bridge_splice16(void * /*ctx*/, std::string_view /*path*/,
                              std::uint64_t /*offset*/,
                              int /*pipe_write_fd*/) noexcept {
        return 16;
    }

    CrashDumpPatchOps make_ops(FakeCrashDump &fake, bool with_read) noexcept {
        CrashDumpPatchOps ops{};
        ops.write.ctx = &fake;
        ops.write.write16 = &fake_write16;
        ops.read16 = with_read ? &fake_read16 : nullptr;
        return ops;
    }
} // namespace

int main() {
    /* ---- embedded plan: raw, padded, hash and zero tail. ---- */
    {
        const auto plan = splice_helper_plan();
        assert(plan.raw != nullptr);
        assert(plan.padded != nullptr);
        assert(plan.raw_size == 1432U);
        assert(plan.padded_size == 1440U);
        assert((plan.padded_size % kCrashDumpBlockBytes) == 0U);
        assert(plan.raw_size <= plan.padded_size);
        assert(plan.sha256 != nullptr);
        assert(std::strcmp(plan.sha256,
                           "3c0a108e637954c114df21f03bda95eb691e66f60333b9d78439d9550482545c") == 0);
        for (std::size_t i = plan.raw_size; i < plan.padded_size; ++i) {
            assert(plan.padded[i] == 0U);
        }
    }

    /* ---- vendor path predicate. ---- */
    {
        assert(is_vendor_path("/vendor/lib64/libbinderdebug.so"));
        assert(is_vendor_path("/system/vendor/lib64/libx.so"));
        assert(!is_vendor_path("/apex/com.android.runtime/bin/crash_dump64"));
        assert(!is_vendor_path("/system/lib64/libc++.so"));
        assert(!is_vendor_path("/data/local/tmp/target.bin"));
        assert(!is_vendor_path(""));
    }

    /* ---- patch #1 success: every 16-byte block written, offset-16 verify. ---- */
    {
        FakeCrashDump fake{};
        const auto plan = splice_helper_plan();
        const std::size_t blocks = plan.padded_size / kCrashDumpBlockBytes;
        assert(patch_crash_dump(make_ops(fake, true)) == CrashDumpPatchError::None);
        assert(fake.writes == blocks);
        assert(std::memcmp(fake.bytes.data(), plan.padded, plan.padded_size) == 0);
        std::array<std::uint8_t, 16> verify{};
        assert(fake_read16(&fake, kCrashDumpVerifyOffset, verify.data()) == 16);
        /* The write surface always hashes the whole padded span, not just the
         * raw helper; the last 8 bytes are the implicit pad. */
        assert(fake.bytes[plan.raw_size] == 0U);
    }

    /* ---- verify mismatch is fail-closed (upstream "page cache unchanged"). ---- */
    {
        FakeCrashDump fake{};
        fake.corrupt_verify = true;
        assert(patch_crash_dump(make_ops(fake, true)) ==
               CrashDumpPatchError::VerifyMismatch);
    }

    /* ---- a short read is "verify skipped", exactly like upstream. ---- */
    {
        FakeCrashDump fake{};
        fake.read_ok = false;
        assert(patch_crash_dump(make_ops(fake, true)) == CrashDumpPatchError::None);
        const auto plan = splice_helper_plan();
        assert(fake.writes == plan.padded_size / kCrashDumpBlockBytes);
    }

    /* ---- no read surface: writes still apply, verify is skipped. ---- */
    {
        FakeCrashDump fake{};
        assert(patch_crash_dump(make_ops(fake, false)) == CrashDumpPatchError::None);
        assert(fake.writes != 0U);
    }

    /* ---- write failure propagates. ---- */
    {
        FakeCrashDump fake{};
        fake.fail_write = true;
        assert(patch_crash_dump(make_ops(fake, true)) ==
               CrashDumpPatchError::WriteFailed);
        assert(fake.writes == 0U);
    }

    /* ---- no write surface = NotAvailable, fail-closed. ---- */
    {
        FakeCrashDump fake{};
        CrashDumpPatchOps ops{};
        assert(patch_crash_dump(ops) == CrashDumpPatchError::NotAvailable);
        (void)fake;
    }

    /* ---- the bridge surface requires both the read and the helper write
     * entry points (available() is derived, never a separate flag). ---- */
    {
        CrashDumpBridgeOps partial{};
        assert(!partial.available());
        partial.read16 = &fake_bridge_read16;
        assert(!partial.available());
        partial.splice16_into_pipe = &fake_bridge_splice16;
        assert(partial.available());
    }

    /* ---- the real bridge is device-only; on a non-Linux host it is null. ---- */
    {
        const CrashDumpBridgeOps bridge = real_crash_dump_bridge();
#if defined(__linux__)
        assert(bridge.available());
#else
        assert(!bridge.available());
#endif
    }

    std::puts("cve_2026_43284_crash_dump_test: OK");
    return 0;
}
