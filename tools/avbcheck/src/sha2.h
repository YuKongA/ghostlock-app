// SPDX-License-Identifier: MIT
// Minimal SHA-256 / SHA-512 implementations for tools/avbcheck.
//
// These are plain, dependency-free implementations used to recompute AVB
// descriptor digests and Merkle-tree hashes on the device.  They are not
// constant-time and must not be used for secret material.
#ifndef AVBCHECK_SHA2_H_
#define AVBCHECK_SHA2_H_

#include <cstddef>
#include <cstdint>
#include <string>

namespace avbcheck {

class Sha256 {
 public:
  static constexpr std::size_t kDigestSize = 32;
  static constexpr std::size_t kBlockSize = 64;

  Sha256();
  void Reset();
  void Update(const void* data, std::size_t len);
  void Final(uint8_t out[kDigestSize]);

 private:
  void Transform(const uint8_t block[kBlockSize]);

  uint32_t h_[8];
  uint64_t total_len_;
  uint8_t buf_[kBlockSize];
  std::size_t buf_len_;
};

class Sha512 {
 public:
  static constexpr std::size_t kDigestSize = 64;
  static constexpr std::size_t kBlockSize = 128;

  Sha512();
  void Reset();
  void Update(const void* data, std::size_t len);
  void Final(uint8_t out[kDigestSize]);

 private:
  void Transform(const uint8_t block[kBlockSize]);

  uint64_t h_[8];
  uint64_t total_len_lo_;
  uint64_t total_len_hi_;
  uint8_t buf_[kBlockSize];
  std::size_t buf_len_;
};

// Hex helper shared by the CLI and the AVB layer.
std::string BytesToHex(const uint8_t* data, std::size_t len);

}  // namespace avbcheck

#endif  // AVBCHECK_SHA2_H_
