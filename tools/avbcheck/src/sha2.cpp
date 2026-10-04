// SPDX-License-Identifier: MIT
#include "sha2.h"

#include <cstring>

namespace avbcheck {
namespace {

inline uint32_t Rotr32(uint32_t x, uint32_t n) {
  return (x >> n) | (x << (32u - n));
}
inline uint64_t Rotr64(uint64_t x, uint64_t n) {
  return (x >> n) | (x << (64u - n));
}
inline uint32_t LoadBe32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) |
         (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}
inline void StoreBe32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v >> 24);
  p[1] = static_cast<uint8_t>(v >> 16);
  p[2] = static_cast<uint8_t>(v >> 8);
  p[3] = static_cast<uint8_t>(v);
}
inline uint64_t LoadBe64(const uint8_t* p) {
  return (static_cast<uint64_t>(LoadBe32(p)) << 32) |
         static_cast<uint64_t>(LoadBe32(p + 4));
}
inline void StoreBe64(uint8_t* p, uint64_t v) {
  StoreBe32(p, static_cast<uint32_t>(v >> 32));
  StoreBe32(p + 4, static_cast<uint32_t>(v));
}

const uint32_t kK256[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

const uint64_t kK512[80] = {
    0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full,
    0xe9b5dba58189dbbcull, 0x3956c25bf348b538ull, 0x59f111f1b605d019ull,
    0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull, 0xd807aa98a3030242ull,
    0x12835b0145706fbeull, 0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull,
    0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull,
    0xc19bf174cf692694ull, 0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull,
    0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull, 0x2de92c6f592b0275ull,
    0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull,
    0x983e5152ee66dfabull, 0xa831c66d2db43210ull, 0xb00327c898fb213full,
    0xbf597fc7beef0ee4ull, 0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull,
    0x06ca6351e003826full, 0x142929670a0e6e70ull, 0x27b70a8546d22ffcull,
    0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull, 0x53380d139d95b3dfull,
    0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull,
    0x92722c851482353bull, 0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull,
    0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull, 0xd192e819d6ef5218ull,
    0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull,
    0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull, 0x2748774cdf8eeb99ull,
    0x34b0bcb5e19b48a8ull, 0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull,
    0x5b9cca4f7763e373ull, 0x682e6ff3d6b2b8a3ull, 0x748f82ee5defb2fcull,
    0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
    0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull,
    0xc67178f2e372532bull, 0xca273eceea26619cull, 0xd186b8c721c0c207ull,
    0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull, 0x06f067aa72176fbaull,
    0x0a637dc5a2c898a6ull, 0x113f9804bef90daeull, 0x1b710b35131c471bull,
    0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull,
    0x431d67c49c100d4cull, 0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull,
    0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull};

}  // namespace

Sha256::Sha256() { Reset(); }

void Sha256::Reset() {
  h_[0] = 0x6a09e667u;
  h_[1] = 0xbb67ae85u;
  h_[2] = 0x3c6ef372u;
  h_[3] = 0xa54ff53au;
  h_[4] = 0x510e527fu;
  h_[5] = 0x9b05688cu;
  h_[6] = 0x1f83d9abu;
  h_[7] = 0x5be0cd19u;
  total_len_ = 0;
  buf_len_ = 0;
}

void Sha256::Transform(const uint8_t block[kBlockSize]) {
  uint32_t w[64];
  for (int i = 0; i < 16; ++i) {
    w[i] = LoadBe32(block + 4 * i);
  }
  for (int i = 16; i < 64; ++i) {
    const uint32_t s0 = Rotr32(w[i - 15], 7) ^ Rotr32(w[i - 15], 18) ^
                        (w[i - 15] >> 3);
    const uint32_t s1 = Rotr32(w[i - 2], 17) ^ Rotr32(w[i - 2], 19) ^
                        (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
  uint32_t e = h_[4], f = h_[5], g = h_[6], h = h_[7];
  for (int i = 0; i < 64; ++i) {
    const uint32_t s1 = Rotr32(e, 6) ^ Rotr32(e, 11) ^ Rotr32(e, 25);
    const uint32_t ch = (e & f) ^ ((~e) & g);
    const uint32_t t1 = h + s1 + ch + kK256[i] + w[i];
    const uint32_t s0 = Rotr32(a, 2) ^ Rotr32(a, 13) ^ Rotr32(a, 22);
    const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t t2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  h_[0] += a;
  h_[1] += b;
  h_[2] += c;
  h_[3] += d;
  h_[4] += e;
  h_[5] += f;
  h_[6] += g;
  h_[7] += h;
}

void Sha256::Update(const void* data, std::size_t len) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  total_len_ += len;
  while (len > 0) {
    const std::size_t take =
        (len < (kBlockSize - buf_len_)) ? len : (kBlockSize - buf_len_);
    std::memcpy(buf_ + buf_len_, p, take);
    buf_len_ += take;
    p += take;
    len -= take;
    if (buf_len_ == kBlockSize) {
      Transform(buf_);
      buf_len_ = 0;
    }
  }
}

void Sha256::Final(uint8_t out[kDigestSize]) {
  const uint64_t bit_len = total_len_ * 8;
  const uint8_t pad = 0x80;
  Update(&pad, 1);
  const uint8_t zero = 0;
  while (buf_len_ != 56) {
    Update(&zero, 1);
  }
  uint8_t lenbuf[8];
  for (int i = 0; i < 8; ++i) {
    lenbuf[i] = static_cast<uint8_t>(bit_len >> (56 - 8 * i));
  }
  Update(lenbuf, 8);
  for (int i = 0; i < 8; ++i) {
    StoreBe32(out + 4 * i, h_[i]);
  }
  Reset();
}

Sha512::Sha512() { Reset(); }

void Sha512::Reset() {
  h_[0] = 0x6a09e667f3bcc908ull;
  h_[1] = 0xbb67ae8584caa73bull;
  h_[2] = 0x3c6ef372fe94f82bull;
  h_[3] = 0xa54ff53a5f1d36f1ull;
  h_[4] = 0x510e527fade682d1ull;
  h_[5] = 0x9b05688c2b3e6c1full;
  h_[6] = 0x1f83d9abfb41bd6bull;
  h_[7] = 0x5be0cd19137e2179ull;
  total_len_lo_ = 0;
  total_len_hi_ = 0;
  buf_len_ = 0;
}

void Sha512::Transform(const uint8_t block[kBlockSize]) {
  uint64_t w[80];
  for (int i = 0; i < 16; ++i) {
    w[i] = LoadBe64(block + 8 * i);
  }
  for (int i = 16; i < 80; ++i) {
    const uint64_t s0 = Rotr64(w[i - 15], 1) ^ Rotr64(w[i - 15], 8) ^
                        (w[i - 15] >> 7);
    const uint64_t s1 = Rotr64(w[i - 2], 19) ^ Rotr64(w[i - 2], 61) ^
                        (w[i - 2] >> 6);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint64_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
  uint64_t e = h_[4], f = h_[5], g = h_[6], h = h_[7];
  for (int i = 0; i < 80; ++i) {
    const uint64_t s1 = Rotr64(e, 14) ^ Rotr64(e, 18) ^ Rotr64(e, 41);
    const uint64_t ch = (e & f) ^ ((~e) & g);
    const uint64_t t1 = h + s1 + ch + kK512[i] + w[i];
    const uint64_t s0 = Rotr64(a, 28) ^ Rotr64(a, 34) ^ Rotr64(a, 39);
    const uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
    const uint64_t t2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  h_[0] += a;
  h_[1] += b;
  h_[2] += c;
  h_[3] += d;
  h_[4] += e;
  h_[5] += f;
  h_[6] += g;
  h_[7] += h;
}

void Sha512::Update(const void* data, std::size_t len) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  total_len_lo_ += len;
  if (total_len_lo_ < len) {
    total_len_hi_ += 1;
  }
  while (len > 0) {
    const std::size_t take =
        (len < (kBlockSize - buf_len_)) ? len : (kBlockSize - buf_len_);
    std::memcpy(buf_ + buf_len_, p, take);
    buf_len_ += take;
    p += take;
    len -= take;
    if (buf_len_ == kBlockSize) {
      Transform(buf_);
      buf_len_ = 0;
    }
  }
}

void Sha512::Final(uint8_t out[kDigestSize]) {
  const uint64_t bit_len_lo = total_len_lo_ * 8;
  const uint64_t bit_len_hi =
      (total_len_hi_ << 3) | (total_len_lo_ >> 61);
  const uint8_t pad = 0x80;
  Update(&pad, 1);
  const uint8_t zero = 0;
  while (buf_len_ != 112) {
    Update(&zero, 1);
  }
  uint8_t lenbuf[16];
  for (int i = 0; i < 8; ++i) {
    lenbuf[i] = static_cast<uint8_t>(bit_len_hi >> (56 - 8 * i));
  }
  for (int i = 0; i < 8; ++i) {
    lenbuf[8 + i] = static_cast<uint8_t>(bit_len_lo >> (56 - 8 * i));
  }
  Update(lenbuf, 16);
  for (int i = 0; i < 8; ++i) {
    StoreBe64(out + 8 * i, h_[i]);
  }
  Reset();
}

std::string BytesToHex(const uint8_t* data, std::size_t len) {
  static const char kHex[] = "0123456789abcdef";
  std::string out;
  out.resize(len * 2);
  for (std::size_t i = 0; i < len; ++i) {
    out[2 * i] = kHex[data[i] >> 4];
    out[2 * i + 1] = kHex[data[i] & 0x0f];
  }
  return out;
}

}  // namespace avbcheck
