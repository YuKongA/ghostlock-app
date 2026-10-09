/** @file tests/write_request_test.cpp
 *  The write request's shape and its own invariants.
 *
 *  Sections: 1. the shape is the pinned one, checked here as well as at compile
 *  time; 2. a request that writes nothing is refused by name; 3. the mode names
 *  round-trip and an unknown name is not silently a mode.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "backend/cve_2026_43499/memory/write_request.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::memory::check_request;
    using ghostlock::backend::cve_2026_43499::memory::RequestVerdict;
    using ghostlock::backend::cve_2026_43499::memory::request_verdict_name;
    using ghostlock::backend::cve_2026_43499::memory::WriteMode;
    using ghostlock::backend::cve_2026_43499::memory::write_mode_of;
    using ghostlock::backend::cve_2026_43499::memory::write_mode_token;
    using ghostlock::backend::cve_2026_43499::memory::WriteRequest;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /* ---- 1: the shape ---- */
    void the_shape_holds() {
        expect(offsetof(WriteRequest, target) == 0U, "the target is first");
        expect(offsetof(WriteRequest, mode) == sizeof(std::uintptr_t),
               "the mode follows the target with no hole");
        expect(sizeof(WriteRequest) <= 3U * sizeof(std::uintptr_t),
               "the request fits in three machine words");
        WriteRequest request;
        expect(request.target == 0U && request.mode == WriteMode::Disabled &&
                   !request.preserve_child,
               "a default request asks for nothing");
    }

    /* ---- 2: what is not a request ---- */
    void empty_requests_are_refused() {
        WriteRequest good{};
        good.target = 0xFFFF000000001000ULL;
        good.mode = WriteMode::Credential;
        expect(check_request(good) == RequestVerdict::Ok, "a real request is accepted");

        WriteRequest no_target = good;
        no_target.target = 0U;
        expect(check_request(no_target) == RequestVerdict::NoTarget,
               "a request without a target is refused");
        expect(std::string_view(request_verdict_name(check_request(no_target))) ==
                   std::string_view("param-invalid"),
               "and it is named param-invalid");

        WriteRequest no_mode = good;
        no_mode.mode = WriteMode::Disabled;
        expect(check_request(no_mode) == RequestVerdict::NoMode,
               "a request with nothing to write is refused");
        expect(std::string_view(request_verdict_name(check_request(no_mode))) ==
                   std::string_view("param-invalid"),
               "and it shares that name on purpose");
    }

    /* ---- 3: the mode names ---- */
    void mode_names_round_trip() {
        expect(write_mode_of(std::string_view("zero")) == WriteMode::Zero, "zero parses");
        expect(write_mode_of(std::string_view("credential")) == WriteMode::Credential,
               "credential parses");
        expect(write_mode_of(std::string_view("disabled")) == WriteMode::Disabled,
               "the disabled token parses to disabled");
        expect(write_mode_of(std::string_view("")) == WriteMode::Disabled,
               "an empty name is not a mode");
        expect(write_mode_of(std::string_view("Zero")) == WriteMode::Disabled,
               "a mode name is case sensitive");
        expect(write_mode_of(std::string_view("zer") ) == WriteMode::Disabled,
               "a near miss is not a mode");
        expect(write_mode_of(std::string_view("credential x")) == WriteMode::Disabled,
               "a longer name is not a mode");
        expect(std::strcmp(write_mode_token(WriteMode::Zero), "zero") == 0 &&
                   std::strcmp(write_mode_token(WriteMode::Credential), "credential") == 0,
               "the tokens round-trip");
        for (const WriteMode mode : {WriteMode::Zero, WriteMode::Credential}) {
            expect(write_mode_of(std::string_view(write_mode_token(mode))) == mode,
                   "each mode parses back from its own token");
        }
    }
}  // namespace

int main() {
    std::printf("write request: shape\n");
    the_shape_holds();
    std::printf("write request: empty requests\n");
    empty_requests_are_refused();
    std::printf("write request: mode names\n");
    mode_names_round_trip();
    if (g_failures != 0U) {
        std::printf("write request: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("write request: PASS (pinned shape + refusals named + modes round-trip)\n");
    return 0;
}
