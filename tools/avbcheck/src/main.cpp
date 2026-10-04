// SPDX-License-Identifier: MIT
// avbcheck - on-device AVB / partition integrity checker (no python, no
// partition copies).  See tools/avbcheck/README.md.
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "avb.h"
#include "sha2.h"

#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

namespace {

using avbcheck::Descriptor;
using avbcheck::DescType;
using avbcheck::Hasher;
using avbcheck::ImageSource;
using avbcheck::OpenSource;
using avbcheck::Vbmeta;

const char kUsage[] =
    "usage: avbcheck <command> [options]\n"
    "\n"
    "  hash <path|by-name:NAME|mapper:NAME>\n"
    "      stream SHA-256 of a file or block device\n"
    "  baseline [--serial S] [--slot SUF] [--out FILE] NAME...\n"
    "      on-device baseline TSV (avb_guard.sh compatible)\n"
    "  check [--out FILE] BASELINE.tsv\n"
    "      recompute and compare an avb_guard.sh baseline TSV\n"
    "  avb-verify [--slot SUF] [--active-slot SUF] [--vbmeta PATH]...\n"
    "             [--image-dir DIR] [--no-chain]\n"
    "      parse vbmeta, recompute AVB hash/hashtree descriptors, verify\n"
    "      signatures (embedded + chained keys)\n"
    "  verity-status\n"
    "      summarize dm-verity device state (dmctl/dmsetup) + boot state\n";

std::string GetProperty(const char* name) {
#if defined(__ANDROID__)
  char buf[PROP_VALUE_MAX] = {0};
  if (__system_property_get(name, buf) > 0) {
    return std::string(buf);
  }
#else
  (void)name;
#endif
  return std::string();
}

bool FileExists(const std::string& path) {
  struct stat st;
  return ::stat(path.c_str(), &st) == 0;
}

bool ResolveDevicePath(const std::string& spec, std::string* out) {
  if (spec.rfind("by-name:", 0) == 0) {
    *out = "/dev/block/by-name/" + spec.substr(8);
    return true;
  }
  if (spec.rfind("mapper:", 0) == 0) {
    *out = "/dev/block/mapper/" + spec.substr(7);
    return true;
  }
  if (!spec.empty() && spec[0] == '/') {
    *out = spec;
    return true;
  }
  const std::string by_name = "/dev/block/by-name/" + spec;
  if (FileExists(by_name)) {
    *out = by_name;
    return true;
  }
  const std::string mapper = "/dev/block/mapper/" + spec;
  if (FileExists(mapper)) {
    *out = mapper;
    return true;
  }
  if (FileExists(spec)) {
    *out = spec;
    return true;
  }
  return false;
}

bool HashSource(ImageSource& src, std::string* hex, uint64_t* bytes) {
  Hasher h("sha256");
  std::vector<uint8_t> buf(1u << 20);
  uint64_t off = 0;
  for (;;) {
    size_t got = 0;
    if (!src.ReadAt(off, buf.data(), buf.size(), &got)) {
      return false;
    }
    if (got == 0) {
      break;
    }
    h.Update(buf.data(), got);
    off += got;
    if (got < buf.size()) {
      break;
    }
  }
  *hex = h.FinalHex();
  *bytes = off;
  return true;
}

std::string ClassForName(const std::string& name) {
  if (name == "metadata" || name == "misc") {
    return "state";
  }
  if (name.size() > 7 && name.compare(name.size() - 7, 7, "-verity") == 0) {
    return "verity";
  }
  if (name.rfind("com.android.", 0) == 0) {
    return "verity";
  }
  return "avb";
}

std::string Basename(const std::string& path) {
  const size_t p = path.find_last_of('/');
  return p == std::string::npos ? path : path.substr(p + 1);
}

bool ReadWholeFile(const std::string& path, std::string* out) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) {
    return false;
  }
  char buf[65536];
  size_t n;
  out->clear();
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
    out->append(buf, n);
  }
  const bool ok = std::ferror(f) == 0;
  std::fclose(f);
  return ok;
}

// ---------------------------------------------------------------------------
// hash
// ---------------------------------------------------------------------------
int CmdHash(int argc, char** argv) {
  if (argc < 1) {
    std::fprintf(stderr, "avbcheck hash: missing target\n");
    return 2;
  }
  std::string target;
  if (!ResolveDevicePath(argv[0], &target)) {
    std::fprintf(stderr, "avbcheck hash: cannot resolve target '%s'\n", argv[0]);
    return 2;
  }
  std::string err;
  std::unique_ptr<ImageSource> src = OpenSource(target, &err);
  if (!src) {
    std::fprintf(stderr, "avbcheck hash: %s\n", err.c_str());
    return 1;
  }
  std::string hex;
  uint64_t bytes = 0;
  if (!HashSource(*src, &hex, &bytes)) {
    std::fprintf(stderr, "avbcheck hash: read failed for %s\n", target.c_str());
    return 1;
  }
  std::printf("%s  %llu  %s\n", hex.c_str(),
              static_cast<unsigned long long>(bytes), argv[0]);
  return 0;
}

// ---------------------------------------------------------------------------
// baseline
// ---------------------------------------------------------------------------
void WriteBaselineHeader(FILE* f, const std::string& serial,
                         const std::string& slot, const std::string& image_dir) {
  std::fprintf(f, "# ghostlock avb_guard baseline v1 (TSV, columns below)\n");
  std::fprintf(f, "# serial=%s\n", serial.c_str());
  std::fprintf(f, "# date=on-device\n");
  std::fprintf(f, "# image_dir=%s\n",
               image_dir.empty() ? "(device nodes)" : image_dir.c_str());
  std::fprintf(f, "# columns=name\tclass\tsize\tsha256\tsource\n");
  std::fprintf(f, "# device=%s\n", GetProperty("ro.product.device").c_str());
  std::fprintf(f, "# model=%s\n", GetProperty("ro.product.model").c_str());
  std::fprintf(f, "# release=%s\n",
               GetProperty("ro.build.version.release").c_str());
  std::fprintf(f, "# fingerprint=%s\n",
               GetProperty("ro.build.fingerprint").c_str());
  std::fprintf(f, "# verifiedbootstate=%s\n",
               GetProperty("ro.boot.verifiedbootstate").c_str());
  std::fprintf(f, "# vbmeta_device_state=%s\n",
               GetProperty("ro.boot.vbmeta.device_state").c_str());
  std::fprintf(f, "# slot=%s\n",
               slot.empty() ? GetProperty("ro.boot.slot_suffix").c_str()
                            : slot.c_str());
  std::fprintf(f, "# avb_version=%s\n",
               GetProperty("ro.boot.avb_version").c_str());
  std::fprintf(f, "# tool=avbcheck (on-device)\n");
}

int CmdBaseline(int argc, char** argv) {
  std::string serial;
  std::string slot;
  std::string out_path;
  std::vector<std::string> names;
  int i = 0;
  while (i < argc) {
    const std::string a = argv[i];
    if (a == "--serial" && i + 1 < argc) {
      serial = argv[++i];
    } else if (a == "--slot" && i + 1 < argc) {
      slot = argv[++i];
    } else if (a == "--out" && i + 1 < argc) {
      out_path = argv[++i];
    } else {
      names.push_back(a);
    }
    ++i;
  }
  if (names.empty()) {
    std::fprintf(stderr, "avbcheck baseline: no partition/file names given\n");
    return 2;
  }
  FILE* f = out_path.empty() ? stdout : std::fopen(out_path.c_str(), "w");
  if (f == nullptr) {
    std::fprintf(stderr, "avbcheck baseline: cannot write %s\n",
                 out_path.c_str());
    return 2;
  }
  if (serial.empty()) {
    serial = GetProperty("ro.serialno");
    if (serial.empty()) {
      serial = "unknown";
    }
  }
  WriteBaselineHeader(f, serial, slot, "");
  int total = 0;
  int absent = 0;
  for (const std::string& name : names) {
    std::string path;
    if (!ResolveDevicePath(name, &path)) {
      std::fprintf(f, "# absent: %s (device node missing)\n", name.c_str());
      std::fprintf(stderr, "[skip] %s (absent)\n", name.c_str());
      ++absent;
      continue;
    }
    std::string err;
    std::unique_ptr<ImageSource> src = OpenSource(path, &err);
    if (!src) {
      std::fprintf(f, "# absent: %s (%s)\n", name.c_str(), err.c_str());
      std::fprintf(stderr, "[ERR]  %s (%s)\n", name.c_str(), err.c_str());
      ++absent;
      continue;
    }
    std::string hex;
    uint64_t bytes = 0;
    if (!HashSource(*src, &hex, &bytes)) {
      std::fprintf(f, "# absent: %s (read failed)\n", name.c_str());
      std::fprintf(stderr, "[ERR]  %s (read failed)\n", name.c_str());
      ++absent;
      continue;
    }
    const std::string cls = ClassForName(name);
    std::fprintf(f, "%s\t%s\t%llu\t%s\tdevice:%s:%s\n", name.c_str(),
                 cls.c_str(), static_cast<unsigned long long>(bytes),
                 hex.c_str(), serial.c_str(), path.c_str());
    std::fprintf(stderr, "[baseline] %-22s class=%-6s size=%-12llu sha256=%s\n",
                 name.c_str(), cls.c_str(),
                 static_cast<unsigned long long>(bytes), hex.c_str());
    ++total;
  }
  if (f != stdout) {
    std::fclose(f);
  }
  std::fprintf(stderr, "# baseline entries=%d absent=%d\n", total, absent);
  return total > 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// check
// ---------------------------------------------------------------------------
int CmdCheck(int argc, char** argv) {
  std::string baseline;
  std::string out_path;
  int i = 0;
  while (i < argc) {
    const std::string a = argv[i];
    if (a == "--out" && i + 1 < argc) {
      out_path = argv[++i];
    } else if (a == "--report" && i + 1 < argc) {
      out_path = argv[++i];
    } else {
      baseline = a;
    }
    ++i;
  }
  if (baseline.empty()) {
    std::fprintf(stderr, "avbcheck check: missing baseline.tsv\n");
    return 2;
  }
  std::string data;
  if (!ReadWholeFile(baseline, &data)) {
    std::fprintf(stderr, "avbcheck check: cannot read %s\n", baseline.c_str());
    return 2;
  }
  FILE* f = out_path.empty() ? stdout : std::fopen(out_path.c_str(), "w");
  if (f == nullptr) {
    std::fprintf(stderr, "avbcheck check: cannot write %s\n", out_path.c_str());
    return 2;
  }
  std::fprintf(f, "# ghostlock avb_guard check (on-device avbcheck)\n");
  std::fprintf(f, "# baseline=%s slot=%s\n", baseline.c_str(),
               GetProperty("ro.boot.slot_suffix").c_str());
  int total = 0, oks = 0, fails = 0, errs = 0, vars = 0;
  size_t pos = 0;
  while (pos <= data.size()) {
    size_t eol = data.find('\n', pos);
    if (eol == std::string::npos) {
      eol = data.size();
    }
    const std::string line = data.substr(pos, eol - pos);
    pos = eol + 1;
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::vector<std::string> cols;
    size_t start = 0;
    for (;;) {
      const size_t tab = line.find('\t', start);
      if (tab == std::string::npos) {
        cols.push_back(line.substr(start));
        break;
      }
      cols.push_back(line.substr(start, tab - start));
      start = tab + 1;
    }
    if (cols.size() < 4) {
      continue;
    }
    const std::string& name = cols[0];
    const std::string cls = cols.size() > 1 ? cols[1] : std::string();
    const std::string& expected = cols[3];
    ++total;
    std::string path;
    if (!ResolveDevicePath(name, &path)) {
      std::fprintf(f, "[ERR]  %s (%s) device node missing\n", name.c_str(),
                   cls.c_str());
      ++errs;
      continue;
    }
    std::string err;
    std::unique_ptr<ImageSource> src = OpenSource(path, &err);
    if (!src) {
      std::fprintf(f, "[ERR]  %s (%s) %s\n", name.c_str(), cls.c_str(),
                   err.c_str());
      ++errs;
      continue;
    }
    std::string hex;
    uint64_t bytes = 0;
    if (!HashSource(*src, &hex, &bytes)) {
      std::fprintf(f, "[ERR]  %s (%s) read failed\n", name.c_str(), cls.c_str());
      ++errs;
      continue;
    }
    if (hex == expected) {
      std::fprintf(f, "[ok]   %s (%s)\n", name.c_str(), cls.c_str());
      ++oks;
    } else if (cls == "state") {
      std::fprintf(f, "[var]  %s (%s) expected-variable expected=%s got=%s\n",
                   name.c_str(), cls.c_str(), expected.c_str(), hex.c_str());
      ++vars;
    } else {
      std::fprintf(f, "[FAIL] %s (%s) expected=%s got=%s\n", name.c_str(),
                   cls.c_str(), expected.c_str(), hex.c_str());
      ++fails;
    }
  }
  std::fprintf(f, "# summary total=%d ok=%d fail=%d err=%d expected_variable=%d\n",
               total, oks, fails, errs, vars);
  const bool consistent = fails == 0 && errs == 0;
  std::fprintf(f, "# RESULT=%s\n", consistent ? "CONSISTENT" : "INCONSISTENT");
  if (f != stdout) {
    std::fclose(f);
  }
  std::fprintf(stderr, "CHECK_RESULT=%s\n",
               consistent ? "CONSISTENT" : "INCONSISTENT");
  return consistent ? 0 : 1;
}

// ---------------------------------------------------------------------------
// avb-verify
// ---------------------------------------------------------------------------
struct VerifyCtx {
  std::string slot;
  std::string active_slot;
  std::string image_dir;
  bool follow_chain = true;
  bool show_digests = false;
  int ok = 0, fail = 0, skip = 0, err = 0;
  int sig_ok = 0, sig_fail = 0, sig_skip = 0;
};

std::string JoinPath(const std::string& a, const std::string& b) {
  if (a.empty()) {
    return b;
  }
  if (a.back() == '/') {
    return a + b;
  }
  return a + "/" + b;
}

bool FirstExisting(const std::vector<std::string>& candidates,
                   std::string* out) {
  for (const std::string& c : candidates) {
    if (FileExists(c)) {
      *out = c;
      return true;
    }
  }
  return false;
}

bool ResolveHashTarget(const VerifyCtx& ctx, const std::string& name,
                       std::string* out) {
  std::vector<std::string> cands;
  if (!ctx.image_dir.empty()) {
    cands.push_back(JoinPath(ctx.image_dir, name + ctx.slot + ".img"));
    cands.push_back(JoinPath(ctx.image_dir, name + ".img"));
  } else {
    cands.push_back("/dev/block/by-name/" + name + ctx.slot);
    cands.push_back("/dev/block/by-name/" + name);
    cands.push_back("/dev/block/mapper/" + name + ctx.slot);
    cands.push_back("/dev/block/mapper/" + name);
  }
  return FirstExisting(cands, out);
}

bool ResolveHashtreeTarget(const VerifyCtx& ctx, const std::string& name,
                           std::string* out) {
  const bool slot_is_active =
      ctx.active_slot.empty() || ctx.slot.empty() ||
      ctx.slot == ctx.active_slot;
  std::vector<std::string> cands;
  if (!ctx.image_dir.empty()) {
    if (slot_is_active) {
      cands.push_back(JoinPath(ctx.image_dir, name + "-verity.img"));
    }
    cands.push_back(JoinPath(ctx.image_dir, name + ctx.slot + ".img"));
    cands.push_back(JoinPath(ctx.image_dir, name + ".img"));
  } else {
    if (slot_is_active) {
      cands.push_back("/dev/block/mapper/" + name + "-verity");
    }
    cands.push_back("/dev/block/by-name/" + name + ctx.slot);
    cands.push_back("/dev/block/mapper/" + name + ctx.slot);
    cands.push_back("/dev/block/by-name/" + name);
    cands.push_back("/dev/block/mapper/" + name);
  }
  return FirstExisting(cands, out);
}

bool ResolveChainTarget(const VerifyCtx& ctx, const std::string& name,
                        std::string* out) {
  std::vector<std::string> cands;
  if (!ctx.image_dir.empty()) {
    cands.push_back(JoinPath(ctx.image_dir, name + ctx.slot + ".img"));
    cands.push_back(JoinPath(ctx.image_dir, name + ".img"));
  } else {
    cands.push_back("/dev/block/by-name/" + name + ctx.slot);
    cands.push_back("/dev/block/by-name/" + name);
    cands.push_back("/dev/block/mapper/" + name + ctx.slot);
    cands.push_back("/dev/block/mapper/" + name);
  }
  return FirstExisting(cands, out);
}

void Indent(int depth) {
  for (int i = 0; i < depth; ++i) {
    std::printf("  ");
  }
}

void ReportSignature(VerifyCtx* ctx, const Vbmeta& vb,
                     const std::vector<uint8_t>* required_pubkey,
                     const std::string& label, int depth) {
  std::string detail;
  const avbcheck::SigStatus st =
      avbcheck::VerifyVbmetaSignature(vb, required_pubkey, &detail);
  Indent(depth);
  switch (st) {
    case avbcheck::SigStatus::kOk:
      ++ctx->sig_ok;
      if (required_pubkey != nullptr) {
        std::printf("[sig-ok] chain %s (pubkey sha1 %s)\n", label.c_str(),
                    avbcheck::Sha1Hex(*required_pubkey).c_str());
      } else {
        std::printf("[sig-ok] %s (embedded key)\n", label.c_str());
      }
      break;
    case avbcheck::SigStatus::kFail:
      ++ctx->sig_fail;
      ++ctx->fail;
      if (required_pubkey != nullptr) {
        std::printf("[sig-FAIL] chain %s: %s\n", label.c_str(), detail.c_str());
      } else {
        std::printf("[sig-FAIL] %s: %s\n", label.c_str(), detail.c_str());
      }
      break;
    case avbcheck::SigStatus::kUnsigned:
    case avbcheck::SigStatus::kUnsupported:
    case avbcheck::SigStatus::kError:
      ++ctx->sig_skip;
      std::printf("[sig-skip] %s: %s\n", label.c_str(), detail.c_str());
      break;
  }
}

void ProcessVbmeta(VerifyCtx* ctx, const std::string& path,
                   const std::vector<uint8_t>* required_pubkey, int depth);

void ProcessDescriptor(VerifyCtx* ctx, const Descriptor& d,
                       const std::string& /*vb_label*/, int depth) {
  const bool is_hash = d.type == DescType::kHash;
  const bool is_tree = d.type == DescType::kHashtree;
  const bool is_chain = d.type == DescType::kChain;
  if (!is_hash && !is_tree && !is_chain) {
    return;
  }
  if (is_chain) {
    std::string child;
    Indent(depth);
    if (!ResolveChainTarget(*ctx, d.partition_name, &child)) {
      std::printf("[skip] chain %s (no vbmeta target; slot=%s)\n",
                  d.partition_name.c_str(), ctx->slot.c_str());
      ++ctx->skip;
      return;
    }
    if (!ctx->follow_chain) {
      std::printf("[skip] chain %s (%s; --no-chain)\n",
                  d.partition_name.c_str(), child.c_str());
      ++ctx->skip;
      return;
    }
    std::printf("-- chain %s -> %s\n", d.partition_name.c_str(), child.c_str());
    ProcessVbmeta(ctx, child, &d.public_key, depth);
    return;
  }

  std::string target;
  if (is_hash) {
    if (!ResolveHashTarget(*ctx, d.partition_name, &target)) {
      Indent(depth);
      std::printf("[skip] hash %s (no target; slot=%s)\n",
                  d.partition_name.c_str(), ctx->slot.c_str());
      ++ctx->skip;
      return;
    }
  } else {
    if (!ResolveHashtreeTarget(*ctx, d.partition_name, &target)) {
      Indent(depth);
      std::printf("[skip] hashtree %s (no target; slot=%s active=%s)\n",
                  d.partition_name.c_str(), ctx->slot.c_str(),
                  ctx->active_slot.c_str());
      ++ctx->skip;
      return;
    }
  }

  std::string err;
  std::unique_ptr<ImageSource> src = OpenSource(target, &err);
  Indent(depth);
  if (!src) {
    std::printf("[ERR]  %s %s (%s)\n", is_hash ? "hash" : "hashtree",
                d.partition_name.c_str(), err.c_str());
    ++ctx->err;
    return;
  }
  std::string got;
  std::string rerr;
  if (!avbcheck::RecomputeDescriptorDigest(*src, d, &got, &rerr)) {
    std::printf("[ERR]  %s %s (%s): %s\n", is_hash ? "hash" : "hashtree",
                d.partition_name.c_str(), Basename(target).c_str(),
                rerr.c_str());
    ++ctx->err;
    return;
  }
  const std::vector<uint8_t>& expected_bytes =
      is_hash ? d.digest : d.root_digest;
  if (expected_bytes.empty()) {
    std::printf("[skip] %s %s -> %s (no expected digest embedded)\n",
                is_hash ? "hash" : "hashtree", d.partition_name.c_str(),
                got.c_str());
    ++ctx->skip;
    return;
  }
  const std::string expected =
      avbcheck::BytesToHex(expected_bytes.data(), expected_bytes.size());
  if (ctx->show_digests) {
    Indent(depth);
    std::printf("# digest %s %s %s\n", is_hash ? "hash" : "hashtree",
                d.partition_name.c_str(), got.c_str());
  }
  if (got == expected) {
    std::printf("[ok]   %s %s -> %s (%s)\n", is_hash ? "hash" : "hashtree",
                d.partition_name.c_str(), Basename(target).c_str(),
                d.hash_algorithm.c_str());
    ++ctx->ok;
  } else {
    std::printf("[FAIL] %s %s expected=%s got=%s (%s)\n",
                is_hash ? "hash" : "hashtree", d.partition_name.c_str(),
                expected.c_str(), got.c_str(), Basename(target).c_str());
    ++ctx->fail;
  }
}

void ProcessVbmeta(VerifyCtx* ctx, const std::string& path,
                   const std::vector<uint8_t>* required_pubkey, int depth) {
  Indent(depth);
  std::string err;
  std::unique_ptr<ImageSource> src = OpenSource(path, &err);
  if (!src) {
    std::printf("[ERR]  cannot open vbmeta %s (%s)\n", path.c_str(),
                err.c_str());
    ++ctx->err;
    return;
  }
  Vbmeta vb;
  if (!avbcheck::ParseVbmetaImage(*src, &vb, &err)) {
    std::printf("[ERR]  cannot parse vbmeta %s (%s)\n", path.c_str(),
                err.c_str());
    ++ctx->err;
    return;
  }
  std::printf("### %s (%zu descriptors, %s)\n", Basename(path).c_str(),
              vb.descriptors.size(), vb.header.release_string.c_str());
  ReportSignature(ctx, vb, required_pubkey, Basename(path), depth);
  for (const Descriptor& d : vb.descriptors) {
    ProcessDescriptor(ctx, d, Basename(path), depth + 1);
  }
}

int CmdAvbVerify(int argc, char** argv) {
  VerifyCtx ctx;
  std::vector<std::string> vmetas;
  bool no_chain = false;
  int i = 0;
  while (i < argc) {
    const std::string a = argv[i];
    if (a == "--active-slot" && i + 1 < argc) {
      ctx.active_slot = argv[++i];
    } else if (a == "--slot" && i + 1 < argc) {
      ctx.slot = argv[++i];
    } else if (a == "--image-dir" && i + 1 < argc) {
      ctx.image_dir = argv[++i];
    } else if (a == "--vbmeta" && i + 1 < argc) {
      vmetas.push_back(argv[++i]);
    } else if (a == "--no-chain") {
      no_chain = true;
    } else if (a == "--show-digests") {
      ctx.show_digests = true;
    } else {
      std::fprintf(stderr, "avbcheck avb-verify: unknown option %s\n",
                   a.c_str());
      return 2;
    }
    ++i;
  }
  ctx.follow_chain = !no_chain;
  if (ctx.active_slot.empty()) {
    ctx.active_slot = GetProperty("ro.boot.slot_suffix");
  }
  if (ctx.slot.empty()) {
    ctx.slot = ctx.active_slot.empty() ? std::string("_a") : ctx.active_slot;
  }
  if (vmetas.empty()) {
    const std::string p = "/dev/block/by-name/vbmeta" + ctx.slot;
    if (!FileExists(p)) {
      std::fprintf(stderr, "avbcheck avb-verify: %s not found\n", p.c_str());
      return 1;
    }
    vmetas.push_back(p);
  }
  std::printf("# avbcheck avb-verify slot=%s active_slot=%s image_dir=%s\n",
              ctx.slot.c_str(), ctx.active_slot.c_str(),
              ctx.image_dir.empty() ? "(device)" : ctx.image_dir.c_str());
  for (const std::string& v : vmetas) {
    ProcessVbmeta(&ctx, v, nullptr, 0);
  }
  std::printf("# summary ok=%d fail=%d skip=%d err=%d "
              "sig_ok=%d sig_fail=%d sig_skip=%d\n",
              ctx.ok, ctx.fail, ctx.skip, ctx.err, ctx.sig_ok, ctx.sig_fail,
              ctx.sig_skip);
  const bool soft = ctx.fail == 0 && ctx.err == 0;
  std::printf("AVB_VERIFY=%s\n", soft ? "OK" : "FAIL");
  return soft ? 0 : 1;
}

// ---------------------------------------------------------------------------
// verity-status
// ---------------------------------------------------------------------------
std::string RunCommand(const std::string& cmd, int* rc) {
  FILE* p = ::popen(cmd.c_str(), "r");
  if (p == nullptr) {
    if (rc != nullptr) {
      *rc = -1;
    }
    return std::string();
  }
  std::string out;
  char buf[512];
  while (std::fgets(buf, sizeof(buf), p) != nullptr) {
    out += buf;
  }
  const int status = ::pclose(p);
  if (rc != nullptr) {
    *rc = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  }
  return out;
}

std::string ReadSmallFile(const std::string& path) {
  std::string out;
  FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) {
    return out;
  }
  char buf[256];
  const size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
  buf[n] = '\0';
  out = buf;
  std::fclose(f);
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
    out.pop_back();
  }
  return out;
}

// Extracts the " V"/" E" marker that follows "verity," in dm status output.
char ParseVerityStatus(const std::string& text) {
  const size_t p = text.find("verity,");
  if (p == std::string::npos) {
    return '?';
  }
  size_t q = p + 7;
  while (q < text.size() && (text[q] == ' ' || text[q] == '\t')) {
    ++q;
  }
  if (q < text.size() && (text[q] == 'V' || text[q] == 'E')) {
    return text[q];
  }
  return '?';
}

int CmdVerityStatus(int /*argc*/, char** /*argv*/) {
  std::vector<std::string> names;
  DIR* d = ::opendir("/sys/block");
  if (d != nullptr) {
    struct dirent* ent;
    while ((ent = ::readdir(d)) != nullptr) {
      const std::string name = ent->d_name;
      if (name.rfind("dm-", 0) != 0) {
        continue;
      }
      const std::string dm_name =
          ReadSmallFile("/sys/block/" + name + "/dm/name");
      if (!dm_name.empty()) {
        names.push_back(dm_name);
      }
    }
    ::closedir(d);
  }
  const bool have_dmctl = FileExists("/system/bin/dmctl");
  const bool have_dmsetup = FileExists("/system/bin/dmsetup") ||
                            FileExists("/usr/sbin/dmsetup");
  int errors = 0;
  int verified = 0;
  int checked = 0;
  if (names.empty() && have_dmctl) {
    const std::string out = RunCommand("/system/bin/dmctl list devices", nullptr);
    size_t pos = 0;
    while ((pos = out.find(" : ", pos)) != std::string::npos) {
      size_t start = pos;
      while (start > 0 && out[start - 1] != '\n') {
        --start;
      }
      std::string name = out.substr(start, pos - start);
      while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) {
        name.pop_back();
      }
      if (!name.empty()) {
        names.push_back(name);
      }
      pos += 3;
    }
  }
  std::printf("# avbcheck verity-status devices=%zu dmctl=%d dmsetup=%d\n",
              names.size(), have_dmctl ? 1 : 0, have_dmsetup ? 1 : 0);
  for (const std::string& name : names) {
    std::string text;
    if (have_dmctl) {
      text = RunCommand("/system/bin/dmctl status " + name, nullptr);
    } else if (have_dmsetup) {
      text = RunCommand("dmsetup status " + name, nullptr);
    }
    const char st = ParseVerityStatus(text);
    if (st == 'V') {
      std::printf("[V] %s\n", name.c_str());
      ++verified;
      ++checked;
    } else if (st == 'E') {
      std::printf("[E] %s\n", name.c_str());
      ++errors;
      ++checked;
    } else {
      // Not a verity target (linear/other) or status unavailable.
      continue;
    }
  }
  const std::string boot_state = GetProperty("ro.boot.verifiedbootstate");
  const std::string vb_state = GetProperty("ro.boot.vbmeta.device_state");
  std::printf("# verifiedbootstate=%s vbmeta_device_state=%s\n",
              boot_state.empty() ? "?" : boot_state.c_str(),
              vb_state.empty() ? "?" : vb_state.c_str());
  if (!have_dmctl && !have_dmsetup) {
    std::printf("# note: neither dmctl nor dmsetup available; "
                "per-target V/E not queried\n");
  }
  std::printf("# summary verity_checked=%d V=%d E=%d\n", checked, verified,
              errors);
  const bool ok = errors == 0;
  std::printf("VERITY_RESULT=%s\n", ok ? "OK" : "ERROR");
  return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fputs(kUsage, stderr);
    return 2;
  }
  const std::string cmd = argv[1];
  if (cmd == "hash") {
    return CmdHash(argc - 2, argv + 2);
  }
  if (cmd == "baseline") {
    return CmdBaseline(argc - 2, argv + 2);
  }
  if (cmd == "check") {
    return CmdCheck(argc - 2, argv + 2);
  }
  if (cmd == "avb-verify") {
    return CmdAvbVerify(argc - 2, argv + 2);
  }
  if (cmd == "verity-status") {
    return CmdVerityStatus(argc - 2, argv + 2);
  }
  if (cmd == "-h" || cmd == "--help" || cmd == "help") {
    std::fputs(kUsage, stdout);
    return 0;
  }
  std::fprintf(stderr, "avbcheck: unknown command '%s'\n", cmd.c_str());
  std::fputs(kUsage, stderr);
  return 2;
}
