#ifndef GHOSTLOCK_BOOTIMG_HEADER_H
#define GHOSTLOCK_BOOTIMG_HEADER_H

#include <cstdint>
#include <span>
#include <string_view>

namespace ghostlock::bootimg {

    struct Header final {
        std::uint32_t version = 0;
        std::uint32_t page_size = 0;
        std::uint32_t kernel_size = 0;
        std::uint32_t ramdisk_size = 0;
        std::uint32_t header_size = 0;
        std::uint32_t kernel_load_size = 0;
        std::uint32_t ramdisk_load_size = 0;
        std::uint32_t signature_size = 0;
    };

    struct KernelSegment final {
        std::size_t offset = 0;
        std::size_t size = 0;
    };

    [[nodiscard]] bool parse_header(
        std::span<const std::uint8_t> raw,
        Header &out,
        KernelSegment &segment_out);

    struct Arm64ImageHeader final {
        std::uint64_t image_size = 0;
        bool valid = false;
    };

    [[nodiscard]] Arm64ImageHeader parse_arm64_image(
        std::span<const std::uint8_t> image);

} // namespace ghostlock::bootimg

#endif // GHOSTLOCK_BOOTIMG_HEADER_H
