/* Host tests for the B6/T5 CVE-2026-43284 composition-root binding.
 *
 * The binding reads the module from a caller-supplied path and takes the device
 * probe by injection, so these tests exercise the fail-closed paths (missing
 * path/file, no carrier) and the module-plan construction without touching a
 * device. The real device setup is compiled only under __linux__ and is
 * covered by the NDK build and the later app-call device gate. */

#include "backend/cve_2026_43284/execution_binding.hpp"

#include "backend/cve_2026_43284/schema.hpp"
#include "platform/device_facts.hpp"
#include "profile/document.hpp"
#include "session/core_session.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

using ghostlock::backend::carrier_path_token_from;
using ghostlock::backend::kCve2026_43284Section;
using ghostlock::backend::cve_2026_43284::bind_production_execution_with;
using ghostlock::backend::cve_2026_43284::ExecutionBindError;
using ghostlock::backend::cve_2026_43284::ExecutionBindResult;
using ghostlock::backend::cve_2026_43284::IpsecSaParams;
using ghostlock::backend::cve_2026_43284::ProductionResources;
using ghostlock::platform::DeviceProbeOps;
using ghostlock::platform::FileFact;
using ghostlock::profile::Document;
using ghostlock::session::CoreSession;

namespace {
    constexpr const char *kModulePath = "/tmp/ghostlock_binding_test_module.ko";

    struct FakeDevice final {
        bool carrier_present = true;
    };

    bool fake_file_fact(void *ctx, const char *path, FileFact &out) noexcept {
        auto *f = static_cast<FakeDevice *>(ctx);
        out = FileFact{};
        out.path.set(path);
        out.exists = f->carrier_present;
        return true;
    }

    DeviceProbeOps make_probe(FakeDevice &device) noexcept {
        DeviceProbeOps ops{};
        ops.ctx = &device;
        ops.file_fact = fake_file_fact;
        return ops;
    }

    void write_module(std::size_t bytes) {
        std::FILE *file = std::fopen(kModulePath, "wb");
        assert(file != nullptr);
        for (std::size_t i = 0U; i < bytes; ++i) {
            const unsigned char byte = static_cast<unsigned char>(i & 0xFFU);
            assert(std::fwrite(&byte, 1U, 1U, file) == 1U);
        }
        assert(std::fclose(file) == 0);
    }

    void test_carrier_token_accessor() {
        Document doc;
        ghostlock::profile::Section &section =
                doc.append_section(kCve2026_43284Section);
        section.add("carrier_path", 3U);
        const auto token = carrier_path_token_from(doc);
        assert(token.has_value());
        assert(*token == 3U);

        Document empty;
        assert(!carrier_path_token_from(empty).has_value());
    }

    void test_missing_module_path_fails_closed() {
        CoreSession session;
        ProductionResources resources{};
        Document doc;
        FakeDevice device{};
        IpsecSaParams sa{};
        const ExecutionBindResult result = bind_production_execution_with(
                session, resources, doc, sa, std::string_view{}, make_probe(device));
        assert(result.error == ExecutionBindError::ModulePathEmpty);
        assert(resources.module.plan.region_count == 0U);
    }

    void test_missing_module_file_fails_closed() {
        CoreSession session;
        ProductionResources resources{};
        Document doc;
        FakeDevice device{};
        IpsecSaParams sa{};
        const ExecutionBindResult result = bind_production_execution_with(
                session, resources, doc, sa,
                "/tmp/ghostlock_binding_missing.ko", make_probe(device));
        assert(result.error == ExecutionBindError::ModuleReadFailed);
        assert(resources.module.plan.region_count == 0U);
    }

    void test_unconfirmed_carrier_falls_back_to_first_default() {
        write_module(128U);
        CoreSession session;
        ProductionResources resources{};
        Document doc; /* no carrier_path token -> token 0 */
        FakeDevice device{};
        device.carrier_present = false;
        IpsecSaParams sa{};
        const ExecutionBindResult result = bind_production_execution_with(
                session, resources, doc, sa, kModulePath, make_probe(device));
        /* A probe that cannot confirm any default must not reject the carrier:
         * the chain can still reach a vendor carrier through the crash-dump
         * bridge, and it fails closed later if the choice is unusable. */
        assert(result.error != ExecutionBindError::CarrierRejected);
        assert(resources.carrier.path.size() != 0U);
        assert(resources.module.plan.region_count == 1U);
        (void)std::remove(kModulePath);
    }

    void test_unknown_carrier_token_fails_closed() {
        write_module(128U);
        CoreSession session;
        ProductionResources resources{};
        Document doc;
        /* carrier_path = 99 is outside the known token range. */
        doc.append_section(kCve2026_43284Section).add("carrier_path", 99U);
        FakeDevice device{};
        IpsecSaParams sa{};
        const ExecutionBindResult result = bind_production_execution_with(
                session, resources, doc, sa, kModulePath, make_probe(device));
        assert(result.error == ExecutionBindError::CarrierRejected);
        (void)std::remove(kModulePath);
    }

    void test_module_plan_built_then_host_fails_closed() {
        write_module(128U);
        CoreSession session;
        ProductionResources resources{};
        Document doc;
        ghostlock::profile::Section &section =
                doc.append_section(kCve2026_43284Section);
        section.add("carrier_path", 1U);
        FakeDevice device{};
        IpsecSaParams sa{};
        const ExecutionBindResult result = bind_production_execution_with(
                session, resources, doc, sa, kModulePath, make_probe(device));
        /* The plan is built before any device setup, and no host may claim a
         * successful production bind. */
        assert(resources.module.plan.region_count == 1U);
        assert(resources.module.module_bytes == 128U);
        assert(resources.module.region.len % 16U == 0U);
        assert(result.error != ExecutionBindError::None);
        (void)std::remove(kModulePath);
    }

} // namespace

int main() {
    test_carrier_token_accessor();
    test_missing_module_path_fails_closed();
    test_missing_module_file_fails_closed();
    test_unconfirmed_carrier_falls_back_to_first_default();
    test_module_plan_built_then_host_fails_closed();
    std::puts("execution_binding_test: ok");
    return 0;
}
