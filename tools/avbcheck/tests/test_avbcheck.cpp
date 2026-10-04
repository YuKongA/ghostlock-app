// SPDX-License-Identifier: MIT
// Host-only unit tests for avbcheck (SHA vectors, AVB parsing, Merkle tree,
// descriptor digest recomputation).  The byte-exact avbtool cross-check lives
// in tests/run_crosscheck.sh.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "avb.h"
#include "sha2.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                     \
    }                                                                   \
  } while (0)

#define CHECK_EQ_STR(got, want)                                          \
  do {                                                                   \
    const std::string g_ = (got);                                        \
    const std::string w_ = (want);                                       \
    if (g_ != w_) {                                                      \
      std::fprintf(stderr, "FAIL %s:%d:\n  got  %s\n  want %s\n",        \
                   __FILE__, __LINE__, g_.c_str(), w_.c_str());          \
      ++g_failures;                                                      \
    }                                                                    \
  } while (0)

void PutBe32(std::vector<uint8_t>* v, uint32_t x) {
  v->push_back(static_cast<uint8_t>(x >> 24));
  v->push_back(static_cast<uint8_t>(x >> 16));
  v->push_back(static_cast<uint8_t>(x >> 8));
  v->push_back(static_cast<uint8_t>(x));
}
void PutBe64(std::vector<uint8_t>* v, uint64_t x) {
  PutBe32(v, static_cast<uint32_t>(x >> 32));
  PutBe32(v, static_cast<uint32_t>(x));
}
void PutBytes(std::vector<uint8_t>* v, const void* p, size_t n) {
  const uint8_t* b = static_cast<const uint8_t*>(p);
  v->insert(v->end(), b, b + n);
}
void PutZeros(std::vector<uint8_t>* v, size_t n) { v->insert(v->end(), n, 0); }

void TestSha256() {
  const struct {
    const char* msg;
    const char* hex;
  } kVectors[] = {
      {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
      {"abc",
       "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
      {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
       "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
  };
  for (const auto& v : kVectors) {
    avbcheck::Sha256 h;
    h.Update(v.msg, std::strlen(v.msg));
    uint8_t out[32];
    h.Final(out);
    CHECK_EQ_STR(avbcheck::BytesToHex(out, sizeof(out)), v.hex);
  }
  // One million 'a'.
  avbcheck::Sha256 h;
  std::vector<char> buf(1000, 'a');
  for (int i = 0; i < 1000; ++i) {
    h.Update(buf.data(), buf.size());
  }
  uint8_t out[32];
  h.Final(out);
  CHECK_EQ_STR(
      avbcheck::BytesToHex(out, sizeof(out)),
      "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

void TestSha512() {
  avbcheck::Sha512 h;
  uint8_t out[64];
  h.Final(out);
  CHECK_EQ_STR(avbcheck::BytesToHex(out, sizeof(out)),
               "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
               "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");
  avbcheck::Sha512 h2;
  h2.Update("abc", 3);
  h2.Final(out);
  CHECK_EQ_STR(avbcheck::BytesToHex(out, sizeof(out)),
               "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
               "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
}

void TestSha1() {
  const std::vector<uint8_t> abc = {'a', 'b', 'c'};
  CHECK_EQ_STR(avbcheck::Sha1Hex(abc),
               "a9993e364706816aba3e25717850c26c9cd0d89d");
}

void TestRounds() {
  CHECK(avbcheck::RoundToMultiple(10, 8) == 16);
  CHECK(avbcheck::RoundToMultiple(16, 8) == 16);
  CHECK(avbcheck::RoundToPow2(1) == 1);
  CHECK(avbcheck::RoundToPow2(32) == 32);
  CHECK(avbcheck::RoundToPow2(33) == 64);
}

void TestLevelOffsets() {
  std::vector<uint64_t> offsets;
  uint64_t tree = 0;
  avbcheck::CalcHashLevelOffsets(24576, 4096, 32, &offsets, &tree);
  CHECK(offsets.size() == 1);
  CHECK(offsets[0] == 0);
  CHECK(tree == 4096);
  avbcheck::CalcHashLevelOffsets(4 * 1024 * 1024, 4096, 32, &offsets, &tree);
  CHECK(offsets.size() == 2);
  CHECK(offsets[0] == 4096);
  CHECK(offsets[1] == 0);
  CHECK(tree == 36864);
}

std::vector<uint8_t> MakeData(size_t n) {
  std::vector<uint8_t> d(n);
  for (size_t i = 0; i < n; ++i) {
    d[i] = static_cast<uint8_t>((i * 7) & 0xff);
  }
  return d;
}

void TestHashTree() {
  // 3 blocks of deterministic data, salt 0f12.  Expected root was produced by
  // avbtool's generate_hash_tree (see tools/device-guard/avb_tool).
  std::vector<uint8_t> data = MakeData(4096 * 3);
  auto src = avbcheck::OpenMemorySource("mem", data);
  std::vector<uint64_t> offsets;
  uint64_t tree_size = 0;
  avbcheck::CalcHashLevelOffsets(data.size(), 4096, 32, &offsets, &tree_size);
  std::vector<uint8_t> salt = {0x0f, 0x12};
  std::vector<uint8_t> root;
  std::vector<uint8_t> tree;
  std::string err;
  CHECK(avbcheck::GenerateHashTree(*src, data.size(), 4096, "sha256", salt, 0,
                                   offsets, tree_size, &root, &tree, &err));
  CHECK_EQ_STR(avbcheck::BytesToHex(root.data(), root.size()),
               "f85a07e2b191409185294737a406bc370597708626a0abf0b0567cfd461a7768");
  CHECK(tree.size() == tree_size);

  // Single-block special case: sha256(salt || block) with salt empty.
  std::vector<uint8_t> one(4096, 0);
  auto src1 = avbcheck::OpenMemorySource("mem1", one);
  std::vector<uint8_t> empty_salt;
  std::vector<uint64_t> off1;
  uint64_t ts1 = 0;
  avbcheck::CalcHashLevelOffsets(one.size(), 4096, 32, &off1, &ts1);
  std::vector<uint8_t> root1;
  CHECK(avbcheck::GenerateHashTree(*src1, one.size(), 4096, "sha256",
                                   empty_salt, 0, off1, ts1, &root1, nullptr,
                                   &err));
  CHECK_EQ_STR(
      avbcheck::BytesToHex(root1.data(), root1.size()),
      "ad7facb2586fc6e966c004d7d1d16b024f5805ff7cb47c7a85dabd8b48892ca7");
}

// Builds a hash descriptor (tag 2).
std::vector<uint8_t> BuildHashDescriptor(const std::string& name,
                                         const std::vector<uint8_t>& salt,
                                         const std::vector<uint8_t>& digest,
                                         uint64_t image_size) {
  std::vector<uint8_t> body;
  PutBe64(&body, image_size);
  std::vector<uint8_t> alg(32, 0);
  std::memcpy(alg.data(), "sha256", 6);
  PutBytes(&body, alg.data(), alg.size());
  PutBe32(&body, static_cast<uint32_t>(name.size()));
  PutBe32(&body, static_cast<uint32_t>(salt.size()));
  PutBe32(&body, static_cast<uint32_t>(digest.size()));
  PutBe32(&body, 0);         // flags
  PutZeros(&body, 60);       // reserved
  PutBytes(&body, name.data(), name.size());
  PutBytes(&body, salt.data(), salt.size());
  PutBytes(&body, digest.data(), digest.size());
  while (body.size() % 8 != 0) {
    body.push_back(0);
  }
  std::vector<uint8_t> desc;
  PutBe64(&desc, 2);
  PutBe64(&desc, body.size());
  PutBytes(&desc, body.data(), body.size());
  return desc;
}

// Builds a chain descriptor (tag 4).
std::vector<uint8_t> BuildChainDescriptor(const std::string& name,
                                          const std::vector<uint8_t>& key) {
  std::vector<uint8_t> body;
  PutBe32(&body, 3);  // rollback index location
  PutBe32(&body, static_cast<uint32_t>(name.size()));
  PutBe32(&body, static_cast<uint32_t>(key.size()));
  PutBe32(&body, 0);
  PutZeros(&body, 60);
  PutBytes(&body, name.data(), name.size());
  PutBytes(&body, key.data(), key.size());
  while (body.size() % 8 != 0) {
    body.push_back(0);
  }
  std::vector<uint8_t> desc;
  PutBe64(&desc, 4);
  PutBe64(&desc, body.size());
  PutBytes(&desc, body.data(), body.size());
  return desc;
}

void TestParseSyntheticVbmeta() {
  const std::string name = "boot";
  const std::vector<uint8_t> salt = {0x0f, 0x12};
  avbcheck::Sha256 h;
  h.Update("abc", 3);
  std::vector<uint8_t> digest(32);
  h.Final(digest.data());

  std::vector<uint8_t> sections;
  const std::vector<uint8_t> hash_desc =
      BuildHashDescriptor(name, salt, digest, 3);
  PutBytes(&sections, hash_desc.data(), hash_desc.size());
  const std::vector<uint8_t> key(264, 0xab);
  const std::vector<uint8_t> chain_desc = BuildChainDescriptor("vbmeta_system", key);
  PutBytes(&sections, chain_desc.data(), chain_desc.size());

  std::vector<uint8_t> blob;
  PutBytes(&blob, "AVB0", 4);
  PutBe32(&blob, 1);
  PutBe32(&blob, 0);
  PutBe64(&blob, 0);                  // auth_size
  PutBe64(&blob, sections.size());    // aux_size
  PutBe32(&blob, 0);                  // algorithm_type NONE
  for (int i = 0; i < 4; ++i) PutBe64(&blob, 0);  // hash/sig offsets+sizes
  for (int i = 0; i < 2; ++i) PutBe64(&blob, 0);  // pubkey offset/size
  for (int i = 0; i < 2; ++i) PutBe64(&blob, 0);  // pubkey meta
  PutBe64(&blob, 0);                  // descriptors_offset
  PutBe64(&blob, sections.size());    // descriptors_size
  PutBe64(&blob, 0);                  // rollback_index
  PutBe32(&blob, 0);                  // flags
  PutBe32(&blob, 0);                  // rollback_index_location
  std::vector<uint8_t> rel(48, 0);
  std::memcpy(rel.data(), "avbtool 1.3.0", 13);
  PutBytes(&blob, rel.data(), rel.size());
  PutZeros(&blob, 80);  // reserved[80]
  CHECK(blob.size() == 256);
  PutBytes(&blob, sections.data(), sections.size());

  auto src = avbcheck::OpenMemorySource("vbmeta", blob);
  avbcheck::Vbmeta vb;
  std::string err;
  CHECK(avbcheck::ParseVbmetaImage(*src, &vb, &err));
  CHECK(vb.descriptors.size() == 2);
  if (vb.descriptors.size() == 2) {
    const avbcheck::Descriptor& hd = vb.descriptors[0];
    CHECK(hd.type == avbcheck::DescType::kHash);
    CHECK_EQ_STR(hd.partition_name, name);
    CHECK(hd.image_size == 3);
    CHECK_EQ_STR(hd.hash_algorithm, "sha256");
    CHECK(hd.salt == salt);
    CHECK(hd.digest == digest);
    const avbcheck::Descriptor& cd = vb.descriptors[1];
    CHECK(cd.type == avbcheck::DescType::kChain);
    CHECK_EQ_STR(cd.partition_name, "vbmeta_system");
    CHECK(cd.public_key == key);
    CHECK(cd.rollback_index_location == 3);
  }
  CHECK_EQ_STR(vb.header.release_string, "avbtool 1.3.0");
}

void TestRecomputeHash() {
  const std::vector<uint8_t> data = {'a', 'b', 'c'};
  auto src = avbcheck::OpenMemorySource("hash-target", data);
  avbcheck::Descriptor d;
  d.type = avbcheck::DescType::kHash;
  d.hash_algorithm = "sha256";
  d.image_size = 3;
  std::string got;
  std::string err;
  CHECK(avbcheck::RecomputeDescriptorDigest(*src, d, &got, &err));
  CHECK_EQ_STR(got,
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

}  // namespace

int main() {
  TestSha256();
  TestSha512();
  TestSha1();
  TestRounds();
  TestLevelOffsets();
  TestHashTree();
  TestParseSyntheticVbmeta();
  TestRecomputeHash();
  if (g_failures != 0) {
    std::fprintf(stderr, "AVBCHECK_TESTS=FAIL (%d)\n", g_failures);
    return 1;
  }
  std::printf("AVBCHECK_TESTS=PASS\n");
  return 0;
}
