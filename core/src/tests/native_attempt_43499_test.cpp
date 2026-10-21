#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "platform/native_attempt_43499.hpp"

namespace {
    namespace n = ghostlock::platform::native43499;
    std::size_t g_failures = 0U;

    void expect(bool ok, const char *what) {
        if (!ok) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    void put32(std::vector<std::uint8_t> &b, std::size_t at, std::uint32_t v) {
        for (std::size_t i = 0; i < 4U; ++i) b[at + i] = static_cast<std::uint8_t>(v >> (8U * i));
    }
    void put64(std::vector<std::uint8_t> &b, std::size_t at, std::uint64_t v) {
        for (std::size_t i = 0; i < 8U; ++i) b[at + i] = static_cast<std::uint8_t>(v >> (8U * i));
    }

    std::vector<std::uint8_t> good_blob() {
        std::vector<std::uint8_t> b(112U + 64U, 0U);
        put32(b, 0U, 0x474C4E50U);
        b[4U] = 1U;
        b[6U] = 112U;
        put32(b, 8U, 176U);
        put32(b, 12U, 112U);
        put32(b, 16U, 64U);
        b[20U] = 8U;
        put32(b, 24U, 0U);
        put32(b, 28U, 0U);
        put32(b, 32U, 0U);
        put64(b, 36U, UINT64_MAX);
        put32(b, 44U, 2000U);
        put32(b, 48U, 500U);
        put64(b, 52U, 0x1000U);
        put64(b, 60U, 0x2000U);
        put64(b, 68U, 0x3000U);
        put64(b, 76U, 0x4000U);
        b[84U] = 0U;
        b[86U] = 1U;
        b[88U] = 2U;
        return b;
    }
}  // namespace

int main() {
    auto blob = good_blob();
    n::AttemptParams params;
    std::string error;
    expect(n::parse_attempt(blob.data(), blob.size(), params, error), "a valid blob parses");
    expect(params.chain_address == 0x1000U && params.wait_address == 0x2000U &&
               params.target_address == 0x3000U && params.page_sink_address == 0x4000U,
           "addresses land");
    expect(params.page_length == 64U && params.pointer_bytes == 8U && params.join_timeout_ms == 2000U,
           "lengths and timeout land");

    auto bad_magic = blob;
    bad_magic[0U] = 0U;
    expect(!n::parse_attempt(bad_magic.data(), bad_magic.size(), params, error), "bad magic is refused");
    auto bad_align = blob;
    bad_align[12U] = 113U;
    bad_align[16U] = 63U;
    put32(bad_align, 8U, 176U);
    expect(!n::parse_attempt(bad_align.data(), bad_align.size(), params, error), "unaligned page is refused");
    expect(!n::parse_attempt(blob.data(), 10U, params, error), "short blob is refused");

    n::AttemptResult result;
    result.actors_started = true;
    result.requeue_result = 1;
    result.page_stage = 2U;
    result.page_bytes_written = 64U;
    std::uint8_t encoded[48] = {};
    expect(n::encode_result(result, encoded, sizeof(encoded)), "result encodes");
    expect(encoded[24U] == 1U && encoded[26U] == 2U, "result flags land");
    expect(!n::encode_result(result, encoded, 47U), "short result capacity is refused");

#if !defined(__linux__)
    n::AttemptResult ran;
    expect(n::run_attempt(params, ran) == -ENOSYS, "run_attempt is named ENOSYS off Linux");
#endif

    if (g_failures != 0U) {
        std::printf("native_attempt_43499_test: %zu failure(s)\n", g_failures);
        return 1;
    }
    std::printf("native_attempt_43499_test: ok\n");
    return 0;
}
