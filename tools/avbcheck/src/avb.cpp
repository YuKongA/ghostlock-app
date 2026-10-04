// SPDX-License-Identifier: MIT
#include "avb.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__linux__)
#include <linux/fs.h>
#include <sys/ioctl.h>
#endif

namespace avbcheck {
namespace {

constexpr uint64_t kMaxVbmetaBlob = 64ull * 1024ull * 1024ull;

inline uint32_t LoadBe32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) |
         (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}
inline uint64_t LoadBe64(const uint8_t* p) {
  return (static_cast<uint64_t>(LoadBe32(p)) << 32) |
         static_cast<uint64_t>(LoadBe32(p + 4));
}

std::string RstripNul(const std::string& s) {
  size_t end = s.size();
  while (end > 0 && s[end - 1] == '\0') {
    --end;
  }
  return s.substr(0, end);
}

// ---------------------------------------------------------------------------
// Sources.
// ---------------------------------------------------------------------------
class FdSource : public ImageSource {
 public:
  FdSource(int fd, std::string path, uint64_t size)
      : fd_(fd), path_(std::move(path)), size_(size) {}
  ~FdSource() override {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }
  bool ReadAt(uint64_t off, void* buf, size_t len, size_t* got) override {
    uint8_t* p = static_cast<uint8_t*>(buf);
    size_t done = 0;
    while (done < len) {
      const ssize_t n =
          ::pread(fd_, p + done, len - done, static_cast<off_t>(off + done));
      if (n < 0) {
        if (errno == EINTR) {
          continue;
        }
        return false;
      }
      if (n == 0) {
        break;
      }
      done += static_cast<size_t>(n);
    }
    *got = done;
    return true;
  }
  uint64_t Size() override { return size_; }
  const std::string& path() const override { return path_; }

 private:
  int fd_;
  std::string path_;
  uint64_t size_;
};

class MemSource : public ImageSource {
 public:
  MemSource(std::string path, const std::vector<uint8_t>& data)
      : path_(std::move(path)), data_(data) {}
  bool ReadAt(uint64_t off, void* buf, size_t len, size_t* got) override {
    if (off >= data_.size()) {
      *got = 0;
      return true;
    }
    const size_t avail = data_.size() - static_cast<size_t>(off);
    const size_t n = std::min(len, avail);
    std::memcpy(buf, data_.data() + static_cast<size_t>(off), n);
    *got = n;
    return true;
  }
  uint64_t Size() override { return data_.size(); }
  const std::string& path() const override { return path_; }

 private:
  std::string path_;
  std::vector<uint8_t> data_;
};

}  // namespace

bool ImageSource::ReadExact(uint64_t off, void* buf, size_t len,
                            std::string* err) {
  size_t got = 0;
  if (!ReadAt(off, buf, len, &got)) {
    if (err != nullptr) {
      *err = "I/O error reading " + path() + " at offset " +
             std::to_string(off);
    }
    return false;
  }
  if (got != len) {
    if (err != nullptr) {
      *err = "short read from " + path() + " at offset " +
             std::to_string(off) + ": wanted " + std::to_string(len) +
             ", got " + std::to_string(got);
    }
    return false;
  }
  return true;
}

std::unique_ptr<ImageSource> OpenSource(const std::string& path,
                                        std::string* err) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    if (err != nullptr) {
      *err = "cannot open " + path + ": " + std::strerror(errno);
    }
    return nullptr;
  }
  struct stat st;
  if (::fstat(fd, &st) != 0) {
    if (err != nullptr) {
      *err = "fstat(" + path + ") failed: " + std::strerror(errno);
    }
    ::close(fd);
    return nullptr;
  }
  uint64_t size = 0;
  if (S_ISREG(st.st_mode)) {
    size = static_cast<uint64_t>(st.st_size);
  } else if (S_ISBLK(st.st_mode)) {
#if defined(__linux__)
    unsigned long long dev_size = 0;
    if (::ioctl(fd, BLKGETSIZE64, &dev_size) == 0) {
      size = static_cast<uint64_t>(dev_size);
    }
#else
    size = static_cast<uint64_t>(st.st_size);
#endif
  } else {
    size = static_cast<uint64_t>(st.st_size);
  }
  return std::unique_ptr<ImageSource>(new FdSource(fd, path, size));
}

std::unique_ptr<ImageSource> OpenMemorySource(const std::string& name,
                                              const std::vector<uint8_t>& data) {
  return std::unique_ptr<ImageSource>(new MemSource(name, data));
}

// ---------------------------------------------------------------------------
// Hasher.
// ---------------------------------------------------------------------------
Hasher::Hasher(const std::string& algorithm) : kind_(Kind::kNone) {
  const std::string alg = ToLower(algorithm);
  if (alg == "sha256") {
    kind_ = Kind::kSha256;
  } else if (alg == "sha512") {
    kind_ = Kind::kSha512;
  }
}

size_t Hasher::digest_size() const {
  switch (kind_) {
    case Kind::kSha256:
      return Sha256::kDigestSize;
    case Kind::kSha512:
      return Sha512::kDigestSize;
    default:
      return 0;
  }
}

void Hasher::Update(const void* data, size_t len) {
  switch (kind_) {
    case Kind::kSha256:
      s256_.Update(data, len);
      break;
    case Kind::kSha512:
      s512_.Update(data, len);
      break;
    default:
      break;
  }
}

void Hasher::Final(uint8_t* out) {
  switch (kind_) {
    case Kind::kSha256:
      s256_.Final(out);
      break;
    case Kind::kSha512:
      s512_.Final(out);
      break;
    default:
      break;
  }
}

std::string Hasher::FinalHex() {
  uint8_t buf[Sha512::kDigestSize];
  const size_t n = digest_size();
  if (n == 0) {
    return std::string();
  }
  Final(buf);
  return BytesToHex(buf, n);
}

std::string ToLower(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return s;
}

// ---------------------------------------------------------------------------
// Parsing.
// ---------------------------------------------------------------------------
bool ParseVbmetaHeader(const uint8_t* data, size_t len, VbmetaHeader* out,
                       std::string* err) {
  if (len < 256) {
    if (err != nullptr) {
      *err = "vbmeta header too short";
    }
    return false;
  }
  if (std::memcmp(data, "AVB0", 4) != 0) {
    if (err != nullptr) {
      *err = "missing AVB0 magic";
    }
    return false;
  }
  VbmetaHeader h;
  h.required_major = LoadBe32(data + 4);
  h.required_minor = LoadBe32(data + 8);
  h.auth_size = LoadBe64(data + 12);
  h.aux_size = LoadBe64(data + 20);
  h.algorithm_type = LoadBe32(data + 28);
  h.hash_offset = LoadBe64(data + 32);
  h.hash_size = LoadBe64(data + 40);
  h.signature_offset = LoadBe64(data + 48);
  h.signature_size = LoadBe64(data + 56);
  h.public_key_offset = LoadBe64(data + 64);
  h.public_key_size = LoadBe64(data + 72);
  h.public_key_metadata_offset = LoadBe64(data + 80);
  h.public_key_metadata_size = LoadBe64(data + 88);
  h.descriptors_offset = LoadBe64(data + 96);
  h.descriptors_size = LoadBe64(data + 104);
  h.rollback_index = LoadBe64(data + 112);
  h.flags = LoadBe32(data + 120);
  h.rollback_index_location = LoadBe32(data + 124);
  char rel[48];
  std::memcpy(rel, data + 128, 47);
  rel[47] = '\0';
  h.release_string = RstripNul(rel);
  *out = h;
  return true;
}

bool ParseDescriptors(const uint8_t* data, size_t len,
                      std::vector<Descriptor>* out, std::string* err) {
  size_t o = 0;
  while (o + 16 <= len) {
    const uint64_t tag = LoadBe64(data + o);
    const uint64_t nbf = LoadBe64(data + o + 8);
    if (nbf > len - o - 16) {
      if (err != nullptr) {
        *err = "descriptor overruns blob";
      }
      return false;
    }
    const uint8_t* body = data + o + 16;
    const size_t body_len = static_cast<size_t>(nbf);
    Descriptor d;
    d.tag = tag;
    if (tag == 0) {
      d.type = DescType::kProperty;
      if (body_len >= 16) {
        const uint64_t key_size = LoadBe64(body);
        const uint64_t value_size = LoadBe64(body + 8);
        if (key_size <= body_len - 16) {
          d.key.assign(reinterpret_cast<const char*>(body + 16),
                       static_cast<size_t>(key_size));
          const size_t voff = 16 + static_cast<size_t>(key_size) + 1;
          if (voff + value_size <= body_len) {
            d.value.assign(reinterpret_cast<const char*>(body + voff),
                           static_cast<size_t>(value_size));
          }
        }
      }
    } else if (tag == 1) {
      d.type = DescType::kHashtree;
      if (body_len < 180) {
        if (err != nullptr) {
          *err = "hashtree descriptor too short";
        }
        return false;
      }
      d.dm_verity_version = LoadBe32(body);
      d.image_size = LoadBe64(body + 4);
      d.tree_offset = LoadBe64(body + 12);
      d.tree_size = LoadBe64(body + 20);
      d.data_block_size = LoadBe32(body + 28);
      d.hash_block_size = LoadBe32(body + 32);
      d.fec_num_roots = LoadBe32(body + 36);
      d.fec_offset = LoadBe64(body + 40);
      d.fec_size = LoadBe64(body + 48);
      char alg[33];
      std::memcpy(alg, body + 56, 32);
      alg[32] = '\0';
      d.hash_algorithm = RstripNul(alg);
      const uint32_t name_len = LoadBe32(body + 88);
      const uint32_t salt_len = LoadBe32(body + 92);
      const uint32_t root_len = LoadBe32(body + 96);
      d.desc_flags = LoadBe32(body + 100);
      size_t p = 164;
      if (p + name_len + salt_len + root_len > body_len) {
        if (err != nullptr) {
          *err = "hashtree descriptor fields overrun";
        }
        return false;
      }
      d.partition_name.assign(reinterpret_cast<const char*>(body + p),
                              name_len);
      p += name_len;
      d.salt.assign(body + p, body + p + salt_len);
      p += salt_len;
      d.root_digest.assign(body + p, body + p + root_len);
    } else if (tag == 2) {
      d.type = DescType::kHash;
      if (body_len < 132) {
        if (err != nullptr) {
          *err = "hash descriptor too short";
        }
        return false;
      }
      d.image_size = LoadBe64(body);
      char alg[33];
      std::memcpy(alg, body + 8, 32);
      alg[32] = '\0';
      d.hash_algorithm = RstripNul(alg);
      const uint32_t name_len = LoadBe32(body + 40);
      const uint32_t salt_len = LoadBe32(body + 44);
      const uint32_t digest_len = LoadBe32(body + 48);
      d.desc_flags = LoadBe32(body + 52);
      size_t p = 116;
      if (p + name_len + salt_len + digest_len > body_len) {
        if (err != nullptr) {
          *err = "hash descriptor fields overrun";
        }
        return false;
      }
      d.partition_name.assign(reinterpret_cast<const char*>(body + p),
                              name_len);
      p += name_len;
      d.salt.assign(body + p, body + p + salt_len);
      p += salt_len;
      d.digest.assign(body + p, body + p + digest_len);
    } else if (tag == 3) {
      d.type = DescType::kKernelCmdline;
      if (body_len >= 8) {
        d.desc_flags = LoadBe32(body);
        const uint32_t clen = LoadBe32(body + 4);
        if (8 + clen <= body_len) {
          d.kernel_cmdline.assign(reinterpret_cast<const char*>(body + 8),
                                  clen);
        }
      }
    } else if (tag == 4) {
      d.type = DescType::kChain;
      if (body_len < 76) {
        if (err != nullptr) {
          *err = "chain descriptor too short";
        }
        return false;
      }
      d.rollback_index_location = LoadBe32(body);
      const uint32_t name_len = LoadBe32(body + 4);
      const uint32_t key_len = LoadBe32(body + 8);
      d.desc_flags = LoadBe32(body + 12);
      size_t p = 76;
      if (p + name_len + key_len > body_len) {
        if (err != nullptr) {
          *err = "chain descriptor fields overrun";
        }
        return false;
      }
      d.partition_name.assign(reinterpret_cast<const char*>(body + p),
                              name_len);
      p += name_len;
      d.public_key.assign(body + p, body + p + key_len);
    } else {
      d.type = DescType::kUnknown;
    }
    out->push_back(std::move(d));
    o += 16 + static_cast<size_t>(nbf);
  }
  return true;
}

bool ParseVbmetaImage(ImageSource& src, Vbmeta* out, std::string* err) {
  uint64_t offset = 0;
  const uint64_t size = src.Size();
  if (size >= 64) {
    uint8_t footer[64];
    std::string ferr;
    if (src.ReadExact(size - 64, footer, sizeof(footer), &ferr) &&
        std::memcmp(footer, "AVBf", 4) == 0) {
      offset = LoadBe64(footer + 20);
      if (offset >= size) {
        if (err != nullptr) {
          *err = "vbmeta footer offset out of range";
        }
        return false;
      }
    }
  }
  uint8_t header[256];
  std::string herr;
  if (!src.ReadExact(offset, header, sizeof(header), &herr)) {
    if (err != nullptr) {
      *err = herr;
    }
    return false;
  }
  VbmetaHeader h;
  if (!ParseVbmetaHeader(header, sizeof(header), &h, err)) {
    return false;
  }
  const uint64_t total = 256 + h.auth_size + h.aux_size;
  if (total > kMaxVbmetaBlob || total > size - offset) {
    if (err != nullptr) {
      *err = "vbmeta blob size out of range";
    }
    return false;
  }
  std::vector<uint8_t> blob(static_cast<size_t>(total));
  std::string berr;
  if (!src.ReadExact(offset, blob.data(), blob.size(), &berr)) {
    if (err != nullptr) {
      *err = berr;
    }
    return false;
  }
  const uint64_t desc_off = 256 + h.auth_size + h.descriptors_offset;
  if (h.descriptors_size > blob.size() || desc_off > blob.size() ||
      desc_off + h.descriptors_size > blob.size()) {
    if (err != nullptr) {
      *err = "descriptor section out of range";
    }
    return false;
  }
  std::vector<Descriptor> descs;
  std::string derr;
  if (!ParseDescriptors(blob.data() + static_cast<size_t>(desc_off),
                        static_cast<size_t>(h.descriptors_size), &descs,
                        &derr)) {
    if (err != nullptr) {
      *err = derr;
    }
    return false;
  }
  out->header = h;
  out->blob = std::move(blob);
  out->descriptors = std::move(descs);
  return true;
}

// ---------------------------------------------------------------------------
// Hash tree.
// ---------------------------------------------------------------------------
uint64_t RoundToMultiple(uint64_t number, uint64_t size) {
  const uint64_t rem = number % size;
  return rem == 0 ? number : number + size - rem;
}

uint64_t RoundToPow2(uint64_t number) {
  if (number <= 1) {
    return 1;
  }
  uint64_t v = 1;
  while (v < number) {
    v <<= 1;
    if (v == 0) {
      return number;
    }
  }
  return v;
}

void CalcHashLevelOffsets(uint64_t image_size, uint64_t block_size,
                          uint64_t digest_size, std::vector<uint64_t>* offsets,
                          uint64_t* tree_size) {
  offsets->clear();
  std::vector<uint64_t> level_sizes;
  uint64_t size = image_size;
  while (size > block_size) {
    const uint64_t num_blocks = (size + block_size - 1) / block_size;
    const uint64_t level_size =
        RoundToMultiple(num_blocks * digest_size, block_size);
    level_sizes.push_back(level_size);
    size = level_size;
  }
  const size_t num_levels = level_sizes.size();
  for (size_t n = 0; n < num_levels; ++n) {
    uint64_t off = 0;
    for (size_t m = n + 1; m < num_levels; ++m) {
      off += level_sizes[m];
    }
    offsets->push_back(off);
  }
  uint64_t total = 0;
  for (const uint64_t v : level_sizes) {
    total += v;
  }
  *tree_size = total;
}

bool GenerateHashTree(ImageSource& src, uint64_t image_size, uint64_t block_size,
                      const std::string& algorithm,
                      const std::vector<uint8_t>& salt, uint32_t digest_padding,
                      const std::vector<uint64_t>& level_offsets,
                      uint64_t tree_size, std::vector<uint8_t>* root,
                      std::vector<uint8_t>* tree, std::string* err) {
  Hasher probe(algorithm);
  if (!probe.ok()) {
    if (err != nullptr) {
      *err = "unsupported hash algorithm " + algorithm;
    }
    return false;
  }
  const size_t digest_size = probe.digest_size();
  std::vector<uint8_t> hash_ret(static_cast<size_t>(tree_size), 0);
  uint64_t hash_src_size = image_size;
  int level_num = 0;
  std::vector<uint8_t> level_output;

  if (image_size == block_size) {
    Hasher h(algorithm);
    h.Update(salt.data(), salt.size());
    std::vector<uint8_t> data(static_cast<size_t>(block_size));
    std::string rerr;
    if (!src.ReadExact(0, data.data(), data.size(), &rerr)) {
      if (err != nullptr) {
        *err = rerr;
      }
      return false;
    }
    h.Update(data.data(), data.size());
    root->assign(digest_size, 0);
    h.Final(root->data());
    if (tree != nullptr) {
      *tree = std::move(hash_ret);
    }
    return true;
  }

  std::vector<uint8_t> block(static_cast<size_t>(block_size));
  while (hash_src_size > block_size) {
    level_output.clear();
    uint64_t remaining = hash_src_size;
    while (remaining > 0) {
      Hasher h(algorithm);
      h.Update(salt.data(), salt.size());
      size_t want = static_cast<size_t>(
          std::min<uint64_t>(remaining, block_size));
      if (level_num == 0) {
        const uint64_t off = hash_src_size - remaining;
        std::string rerr;
        if (!src.ReadExact(off, block.data(), want, &rerr)) {
          if (err != nullptr) {
            *err = rerr;
          }
          return false;
        }
        h.Update(block.data(), want);
      } else {
        const uint64_t off =
            level_offsets[static_cast<size_t>(level_num - 1)] +
            hash_src_size - remaining;
        h.Update(hash_ret.data() + static_cast<size_t>(off), want);
      }
      remaining -= want;
      if (want < static_cast<size_t>(block_size)) {
        static const uint8_t kZeros[4096] = {0};
        size_t pad = static_cast<size_t>(block_size) - want;
        while (pad > 0) {
          const size_t chunk = pad < sizeof(kZeros) ? pad : sizeof(kZeros);
          h.Update(kZeros, chunk);
          pad -= chunk;
        }
      }
      std::vector<uint8_t> dig(digest_size);
      h.Final(dig.data());
      level_output.insert(level_output.end(), dig.begin(), dig.end());
      level_output.insert(level_output.end(), digest_padding, 0);
    }
    const size_t padded = static_cast<size_t>(
        RoundToMultiple(level_output.size(), block_size));
    level_output.resize(padded, 0);
    const uint64_t off = level_offsets[static_cast<size_t>(level_num)];
    std::memcpy(hash_ret.data() + static_cast<size_t>(off), level_output.data(),
                level_output.size());
    hash_src_size = level_output.size();
    ++level_num;
  }

  Hasher h(algorithm);
  h.Update(salt.data(), salt.size());
  h.Update(level_output.data(), level_output.size());
  root->assign(digest_size, 0);
  h.Final(root->data());
  if (tree != nullptr) {
    *tree = std::move(hash_ret);
  }
  return true;
}

bool RecomputeDescriptorDigest(ImageSource& src, const Descriptor& desc,
                               std::string* out_hex, std::string* err) {
  if (desc.type == DescType::kHash) {
    Hasher h(desc.hash_algorithm);
    if (!h.ok()) {
      if (err != nullptr) {
        *err = "unsupported hash algorithm " + desc.hash_algorithm;
      }
      return false;
    }
    h.Update(desc.salt.data(), desc.salt.size());
    std::vector<uint8_t> buf(1u << 20);
    uint64_t remaining = desc.image_size;
    uint64_t off = 0;
    while (remaining > 0) {
      const size_t want = static_cast<size_t>(
          std::min<uint64_t>(remaining, buf.size()));
      std::string rerr;
      if (!src.ReadExact(off, buf.data(), want, &rerr)) {
        if (err != nullptr) {
          *err = rerr;
        }
        return false;
      }
      h.Update(buf.data(), want);
      off += want;
      remaining -= want;
    }
    *out_hex = h.FinalHex();
    return true;
  }
  if (desc.type == DescType::kHashtree) {
    Hasher h(desc.hash_algorithm);
    if (!h.ok()) {
      if (err != nullptr) {
        *err = "unsupported hash algorithm " + desc.hash_algorithm;
      }
      return false;
    }
    if (desc.data_block_size == 0) {
      if (err != nullptr) {
        *err = "hashtree descriptor has zero block size";
      }
      return false;
    }
    const uint64_t digest_size = h.digest_size();
    const uint64_t digest_padding = RoundToPow2(digest_size) - digest_size;
    std::vector<uint64_t> offsets;
    uint64_t tree_size = 0;
    CalcHashLevelOffsets(desc.image_size, desc.data_block_size,
                         digest_size + digest_padding, &offsets, &tree_size);
    std::vector<uint8_t> root;
    if (!GenerateHashTree(src, desc.image_size, desc.data_block_size,
                          desc.hash_algorithm, desc.salt,
                          static_cast<uint32_t>(digest_padding), offsets,
                          tree_size, &root, nullptr, err)) {
      return false;
    }
    *out_hex = BytesToHex(root.data(), root.size());
    return true;
  }
  if (err != nullptr) {
    *err = "descriptor has no digest";
  }
  return false;
}

// ---------------------------------------------------------------------------
// SHA-1 (for public-key fingerprints only).
// ---------------------------------------------------------------------------
std::string Sha1Hex(const std::vector<uint8_t>& data) {
  uint32_t h[5] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u,
                   0xc3d2e1f0u};
  auto rotr = [](uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32u - n));
  };
  std::vector<uint8_t> msg = data;
  const uint64_t bit_len = static_cast<uint64_t>(data.size()) * 8;
  msg.push_back(0x80);
  while (msg.size() % 64 != 56) {
    msg.push_back(0);
  }
  for (int i = 7; i >= 0; --i) {
    msg.push_back(static_cast<uint8_t>(bit_len >> (8 * i)));
  }
  for (size_t off = 0; off < msg.size(); off += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
      w[i] = LoadBe32(&msg[off + 4 * static_cast<size_t>(i)]);
    }
    for (int i = 16; i < 80; ++i) {
      // leftrotate(x, 1) == rotr(x, 31)
      w[i] = rotr(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 31);
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) {
      uint32_t f = 0, k = 0;
      if (i < 20) {
        f = (b & c) | ((~b) & d);
        k = 0x5a827999u;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ed9eba1u;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8f1bbcdcu;
      } else {
        f = b ^ c ^ d;
        k = 0xca62c1d6u;
      }
      // leftrotate(a, 5) == rotr(a, 27)
      const uint32_t tmp = rotr(a, 27) + f + e + k + w[i];
      e = d;
      d = c;
      c = rotr(b, 2);  // leftrotate(b, 30)
      b = a;
      a = tmp;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
  }
  uint8_t out[20];
  for (int i = 0; i < 5; ++i) {
    out[4 * static_cast<size_t>(i)] = static_cast<uint8_t>(h[i] >> 24);
    out[4 * static_cast<size_t>(i) + 1] = static_cast<uint8_t>(h[i] >> 16);
    out[4 * static_cast<size_t>(i) + 2] = static_cast<uint8_t>(h[i] >> 8);
    out[4 * static_cast<size_t>(i) + 3] = static_cast<uint8_t>(h[i]);
  }
  return BytesToHex(out, sizeof(out));
}

// ---------------------------------------------------------------------------
// Minimal big integer (little-endian 2^32 limbs) + raw RSA.
// ---------------------------------------------------------------------------
namespace {

using Limbs = std::vector<uint32_t>;

void Trim(Limbs* a) {
  while (!a->empty() && a->back() == 0) {
    a->pop_back();
  }
}

int Cmp(const Limbs& a, const Limbs& b) {
  if (a.size() != b.size()) {
    return a.size() < b.size() ? -1 : 1;
  }
  for (size_t i = a.size(); i-- > 0;) {
    if (a[i] != b[i]) {
      return a[i] < b[i] ? -1 : 1;
    }
  }
  return 0;
}

Limbs FromBe(const uint8_t* p, size_t n) {
  Limbs a;
  a.reserve((n + 3) / 4);
  size_t i = n;
  while (i > 0) {
    const size_t start = (i >= 4) ? i - 4 : 0;
    uint32_t limb = 0;
    for (size_t j = start; j < i; ++j) {
      limb = (limb << 8) | p[j];
    }
    a.push_back(limb);
    i = start;
  }
  Trim(&a);
  return a;
}

std::vector<uint8_t> ToBe(const Limbs& a, size_t len) {
  std::vector<uint8_t> out(len, 0);
  for (size_t i = 0; i < a.size(); ++i) {
    const uint32_t v = a[i];
    for (size_t b = 0; b < 4; ++b) {
      const size_t pos = i * 4 + b;
      if (pos < len) {
        out[len - 1 - pos] = static_cast<uint8_t>(v >> (8 * b));
      }
    }
  }
  return out;
}

void SubFrom(Limbs* a, const Limbs& b) {
  int64_t borrow = 0;
  for (size_t i = 0; i < a->size(); ++i) {
    int64_t cur = static_cast<int64_t>((*a)[i]) - borrow;
    if (i < b.size()) {
      cur -= static_cast<int64_t>(b[i]);
    }
    if (cur < 0) {
      cur += (static_cast<int64_t>(1) << 32);
      borrow = 1;
    } else {
      borrow = 0;
    }
    (*a)[i] = static_cast<uint32_t>(cur);
  }
  Trim(a);
}

void AddModInPlace(Limbs* a, const Limbs& b, const Limbs& m) {
  const size_t n = std::max(a->size(), b.size());
  a->resize(n, 0);
  uint64_t carry = 0;
  for (size_t i = 0; i < n; ++i) {
    uint64_t s = carry + (*a)[i];
    if (i < b.size()) {
      s += b[i];
    }
    (*a)[i] = static_cast<uint32_t>(s);
    carry = s >> 32;
  }
  if (carry != 0) {
    a->push_back(static_cast<uint32_t>(carry));
  }
  if (Cmp(*a, m) >= 0) {
    SubFrom(a, m);
  }
}

void DoubleModInPlace(Limbs* a, const Limbs& m) {
  uint32_t carry = 0;
  for (size_t i = 0; i < a->size(); ++i) {
    const uint32_t orig = (*a)[i];
    (*a)[i] = (orig << 1) | carry;
    carry = orig >> 31;
  }
  if (carry != 0) {
    a->push_back(carry);
  }
  if (Cmp(*a, m) >= 0) {
    SubFrom(a, m);
  }
}

size_t BitLength(const Limbs& a) {
  if (a.empty()) {
    return 0;
  }
  size_t bits = (a.size() - 1) * 32;
  uint32_t top = a.back();
  while (top != 0) {
    ++bits;
    top >>= 1;
  }
  return bits;
}

bool BitAt(const Limbs& a, size_t i) {
  const size_t limb = i / 32;
  if (limb >= a.size()) {
    return false;
  }
  return ((a[limb] >> (i % 32)) & 1u) != 0;
}

void MulMod(const Limbs& a, const Limbs& b, const Limbs& m, Limbs* out) {
  Limbs result;
  Limbs tmp = a;
  const size_t bits = BitLength(b);
  for (size_t i = 0; i < bits; ++i) {
    if (BitAt(b, i)) {
      AddModInPlace(&result, tmp, m);
    }
    DoubleModInPlace(&tmp, m);
  }
  *out = std::move(result);
}

// Computes base^65537 mod modulus.
bool ModPow65537(const Limbs& base, const Limbs& modulus, Limbs* out) {
  Limbs b = base;
  int guard = 0;
  while (Cmp(b, modulus) >= 0) {
    SubFrom(&b, modulus);
    if (++guard > 64) {
      return false;
    }
  }
  Limbs result;
  result.push_back(1);
  for (int i = 16; i >= 0; --i) {
    Limbs sq;
    MulMod(result, result, modulus, &sq);
    result = std::move(sq);
    if ((65537 >> i) & 1) {
      Limbs mul;
      MulMod(result, b, modulus, &mul);
      result = std::move(mul);
    }
  }
  *out = std::move(result);
  return true;
}

struct AlgInfo {
  const char* hash;
  uint32_t sig_bytes;
  bool valid;
};

AlgInfo LookupAlgorithm(uint32_t type) {
  switch (type) {
    case 1:
      return {"sha256", 256, true};
    case 2:
      return {"sha256", 512, true};
    case 3:
      return {"sha256", 1024, true};
    case 4:
      return {"sha512", 256, true};
    case 5:
      return {"sha512", 512, true};
    case 6:
      return {"sha512", 1024, true};
    default:
      return {"", 0, false};
  }
}

}  // namespace

SigStatus VerifyVbmetaSignature(const Vbmeta& vb,
                                const std::vector<uint8_t>* required_pubkey,
                                std::string* detail) {
  const VbmetaHeader& h = vb.header;
  if (h.algorithm_type == 0) {
    if (detail != nullptr) {
      *detail = "algorithm NONE (unsigned)";
    }
    return SigStatus::kUnsigned;
  }
  const AlgInfo alg = LookupAlgorithm(h.algorithm_type);
  if (!alg.valid) {
    if (detail != nullptr) {
      *detail = "unknown algorithm type " + std::to_string(h.algorithm_type);
    }
    return SigStatus::kUnsupported;
  }
  const size_t hash_len = (std::strcmp(alg.hash, "sha256") == 0) ? 32 : 64;
  if (vb.blob.size() < 256) {
    if (detail != nullptr) {
      *detail = "vbmeta blob too short";
    }
    return SigStatus::kError;
  }
  const uint64_t auth_off = 256;
  const uint64_t aux_off = 256 + h.auth_size;
  if (h.auth_size > vb.blob.size() || aux_off > vb.blob.size() ||
      h.aux_size > vb.blob.size() - aux_off) {
    if (detail != nullptr) {
      *detail = "vbmeta auth/aux block out of range";
    }
    return SigStatus::kError;
  }
  if (h.hash_size != hash_len || h.signature_size != alg.sig_bytes ||
      h.signature_offset + h.signature_size > h.auth_size ||
      h.hash_offset + h.hash_size > h.auth_size) {
    if (detail != nullptr) {
      *detail = "stored hash/signature size does not match algorithm";
    }
    return SigStatus::kError;
  }
  if (h.public_key_size < 8 ||
      h.public_key_offset + h.public_key_size > h.aux_size) {
    if (detail != nullptr) {
      *detail = "embedded public key out of range";
    }
    return SigStatus::kError;
  }

  const uint8_t* header_blob = vb.blob.data();
  const uint8_t* aux_blob = vb.blob.data() + static_cast<size_t>(aux_off);
  const uint8_t* stored_digest =
      vb.blob.data() + static_cast<size_t>(auth_off + h.hash_offset);
  const uint8_t* sig =
      vb.blob.data() + static_cast<size_t>(auth_off + h.signature_offset);
  const uint8_t* pubkey =
      vb.blob.data() + static_cast<size_t>(aux_off + h.public_key_offset);

  Hasher hasher(alg.hash);
  hasher.Update(header_blob, 256);
  hasher.Update(aux_blob, static_cast<size_t>(h.aux_size));
  std::vector<uint8_t> computed(hash_len);
  hasher.Final(computed.data());
  if (std::memcmp(computed.data(), stored_digest, hash_len) != 0) {
    if (detail != nullptr) {
      *detail = "authentication hash mismatch";
    }
    return SigStatus::kFail;
  }

  if (required_pubkey != nullptr) {
    if (required_pubkey->size() != h.public_key_size ||
        std::memcmp(required_pubkey->data(), pubkey, h.public_key_size) != 0) {
      if (detail != nullptr) {
        *detail = "embedded public key does not match chain descriptor";
      }
      return SigStatus::kFail;
    }
  }

  const uint32_t num_bits = LoadBe32(pubkey);
  if (num_bits != alg.sig_bytes * 8 || num_bits % 8 != 0 ||
      h.public_key_size < 8u + num_bits / 8) {
    if (detail != nullptr) {
      *detail = "public key bit length does not match algorithm";
    }
    return SigStatus::kError;
  }

  static const uint8_t kSha256DigestInfo[19] = {
      0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
      0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20};
  static const uint8_t kSha512DigestInfo[19] = {
      0x30, 0x51, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
      0x65, 0x03, 0x04, 0x02, 0x03, 0x05, 0x00, 0x04, 0x40};
  const uint8_t* digest_info =
      (hash_len == 32) ? kSha256DigestInfo : kSha512DigestInfo;
  const size_t k = alg.sig_bytes;
  const size_t t_len = 19 + hash_len;
  if (k < 3 + t_len + 8) {
    if (detail != nullptr) {
      *detail = "key too small for digest info";
    }
    return SigStatus::kError;
  }
  std::vector<uint8_t> expected(k, 0);
  expected[0] = 0x00;
  expected[1] = 0x01;
  const size_t ps_len = k - 3 - t_len;
  std::memset(expected.data() + 2, 0xff, ps_len);
  expected[2 + ps_len] = 0x00;
  std::memcpy(expected.data() + 3 + ps_len, digest_info, 19);
  std::memcpy(expected.data() + 3 + ps_len + 19, computed.data(), hash_len);

  const Limbs modulus = FromBe(pubkey + 8, num_bits / 8);
  if (modulus.empty()) {
    if (detail != nullptr) {
      *detail = "zero modulus";
    }
    return SigStatus::kError;
  }
  const Limbs signature = FromBe(sig, k);
  Limbs recovered;
  if (!ModPow65537(signature, modulus, &recovered)) {
    if (detail != nullptr) {
      *detail = "modular exponentiation failed";
    }
    return SigStatus::kError;
  }
  const std::vector<uint8_t> got = ToBe(recovered, k);
  if (got != expected) {
    if (detail != nullptr) {
      *detail = "RSA signature does not match";
    }
    return SigStatus::kFail;
  }
  if (detail != nullptr) {
    *detail = "ok";
  }
  return SigStatus::kOk;
}

}  // namespace avbcheck
