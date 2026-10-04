// SPDX-License-Identifier: MIT
// AVB (Android Verified Boot) parsing + descriptor digest recomputation.
//
// This is a dependency-free port of the integrity half of AOSP avbtool
// (external/avb/avbtool.py) together with a minimal raw-RSA verifier so the
// whole check can run on the device without python.  See
// tools/device-guard/avb_verify_descriptors.py for the reference host path.
#ifndef AVBCHECK_AVB_H_
#define AVBCHECK_AVB_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "sha2.h"

namespace avbcheck {

// ---------------------------------------------------------------------------
// Random-access byte source (regular file or block device).
// ---------------------------------------------------------------------------
class ImageSource {
 public:
  virtual ~ImageSource() = default;
  // Reads up to |len| bytes at |off|.  |got| receives the number of bytes
  // actually read.  Returns false on a hard I/O error.
  virtual bool ReadAt(uint64_t off, void* buf, size_t len, size_t* got) = 0;
  // Total size in bytes (0 if unknown).
  virtual uint64_t Size() = 0;
  virtual const std::string& path() const = 0;

  // Reads exactly |len| bytes or returns false (sets *err if non-null).
  bool ReadExact(uint64_t off, void* buf, size_t len, std::string* err);
};

// Opens a file or block device read-only.  Returns nullptr and sets |err| on
// failure.
std::unique_ptr<ImageSource> OpenSource(const std::string& path,
                                        std::string* err);
// In-memory source (unit tests / synthetic fixtures).
std::unique_ptr<ImageSource> OpenMemorySource(const std::string& name,
                                              const std::vector<uint8_t>& data);

// ---------------------------------------------------------------------------
// Streaming hash abstraction.
// ---------------------------------------------------------------------------
class Hasher {
 public:
  explicit Hasher(const std::string& algorithm);
  bool ok() const { return kind_ != Kind::kNone; }
  size_t digest_size() const;
  void Update(const void* data, size_t len);
  void Final(uint8_t* out);
  std::string FinalHex();

 private:
  enum class Kind { kNone, kSha256, kSha512 };
  Kind kind_;
  Sha256 s256_;
  Sha512 s512_;
};

// ---------------------------------------------------------------------------
// AVB structures.
// ---------------------------------------------------------------------------
struct VbmetaHeader {
  uint32_t required_major = 0;
  uint32_t required_minor = 0;
  uint64_t auth_size = 0;
  uint64_t aux_size = 0;
  uint32_t algorithm_type = 0;
  uint64_t hash_offset = 0;
  uint64_t hash_size = 0;
  uint64_t signature_offset = 0;
  uint64_t signature_size = 0;
  uint64_t public_key_offset = 0;
  uint64_t public_key_size = 0;
  uint64_t public_key_metadata_offset = 0;
  uint64_t public_key_metadata_size = 0;
  uint64_t descriptors_offset = 0;
  uint64_t descriptors_size = 0;
  uint64_t rollback_index = 0;
  uint32_t flags = 0;
  uint32_t rollback_index_location = 0;
  std::string release_string;
};

enum class DescType {
  kProperty,
  kHashtree,
  kHash,
  kKernelCmdline,
  kChain,
  kUnknown,
};

struct Descriptor {
  uint64_t tag = 0;
  DescType type = DescType::kUnknown;

  // Hash descriptor.
  uint64_t image_size = 0;
  std::string hash_algorithm;
  std::string partition_name;
  std::vector<uint8_t> salt;
  std::vector<uint8_t> digest;

  // Hashtree descriptor.
  uint32_t dm_verity_version = 0;
  uint64_t tree_offset = 0;
  uint64_t tree_size = 0;
  uint32_t data_block_size = 0;
  uint32_t hash_block_size = 0;
  uint32_t fec_num_roots = 0;
  uint64_t fec_offset = 0;
  uint64_t fec_size = 0;
  std::vector<uint8_t> root_digest;
  uint32_t desc_flags = 0;

  // Chain partition descriptor.
  uint32_t rollback_index_location = 0;
  std::vector<uint8_t> public_key;

  // Property / kernel cmdline descriptor.
  std::string key;
  std::string value;
  std::string kernel_cmdline;
};

struct Vbmeta {
  VbmetaHeader header;
  // header + authentication + auxiliary blocks, exactly as hashed/signed.
  std::vector<uint8_t> blob;
  std::vector<Descriptor> descriptors;
};

// Parses |len| bytes of a VBMeta header.  |len| must be >= 256.
bool ParseVbmetaHeader(const uint8_t* data, size_t len, VbmetaHeader* out,
                       std::string* err);
// Parses a descriptor blob (the auxiliary block descriptor section).
bool ParseDescriptors(const uint8_t* data, size_t len,
                      std::vector<Descriptor>* out, std::string* err);
// Reads a whole vbmeta image from |src| (handles the optional AVBf footer).
bool ParseVbmetaImage(ImageSource& src, Vbmeta* out, std::string* err);

// ---------------------------------------------------------------------------
// Hash tree (Merkle) generation, ported from avbtool.
// ---------------------------------------------------------------------------
uint64_t RoundToMultiple(uint64_t number, uint64_t size);
uint64_t RoundToPow2(uint64_t number);
void CalcHashLevelOffsets(uint64_t image_size, uint64_t block_size,
                          uint64_t digest_size, std::vector<uint64_t>* offsets,
                          uint64_t* tree_size);
// Returns the top-level root digest and, if |tree| is non-null, the full
// Merkle tree bytes.
bool GenerateHashTree(ImageSource& src, uint64_t image_size, uint64_t block_size,
                      const std::string& algorithm,
                      const std::vector<uint8_t>& salt, uint32_t digest_padding,
                      const std::vector<uint64_t>& level_offsets,
                      uint64_t tree_size, std::vector<uint8_t>* root,
                      std::vector<uint8_t>* tree, std::string* err);

// Recomputes the digest described by |desc| from the data region of |src| and
// stores the hex digest in |out_hex|.  Returns false and sets |err| on a hard
// error (unsupported algorithm, short read, ...).
bool RecomputeDescriptorDigest(ImageSource& src, const Descriptor& desc,
                               std::string* out_hex, std::string* err);

// ---------------------------------------------------------------------------
// Raw RSA signature verification.
// ---------------------------------------------------------------------------
enum class SigStatus {
  kUnsigned,     // algorithm NONE
  kOk,
  kFail,
  kUnsupported,  // unknown algorithm / unsupported hash
  kError,
};

// Verifies the signature of |vb| using the embedded public key.  When
// |required_pubkey| is non-null the embedded key blob must equal it (chain
// trust link).  |detail| receives a human-readable reason.
SigStatus VerifyVbmetaSignature(const Vbmeta& vb,
                                const std::vector<uint8_t>* required_pubkey,
                                std::string* detail);

// SHA-1 of a public key blob, as avbtool prints for chain descriptors.
std::string Sha1Hex(const std::vector<uint8_t>& data);

// Lowercases an ASCII string.
std::string ToLower(std::string s);

}  // namespace avbcheck

#endif  // AVBCHECK_AVB_H_
