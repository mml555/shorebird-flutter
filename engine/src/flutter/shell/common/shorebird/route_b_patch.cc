// Copyright (c) 2026, the Shorebird self-host fork.

#include "flutter/shell/common/shorebird/route_b_patch.h"

#include <cstdio>
#include <cstring>

#include "flutter/fml/file.h"
#include "flutter/fml/mapping.h"
#include "third_party/rapidjson/include/rapidjson/document.h"

namespace flutter {
namespace route_b {

namespace {

constexpr char kMagic[8] = {'S', 'B', 'R', 'B', 'P', 'T', 'C', 'H'};
constexpr uint32_t kFormatVersion = 1;
// magic(8) + version(4) + headerLen(4). Anything shorter cannot be a container.
constexpr size_t kFixedHeaderSize = 16;

uint32_t ReadU32LE(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

// SHA-256, FIPS 180-4.
//
// Implemented here rather than pulled in because no shell target depends on
// boringssl today and this is an integrity check, not a security boundary --
// the bytes have already been sha256-verified end-to-end by the updater's
// install step by the time we see them. A NIST test vector is asserted in
// route_b_patch_unittests.cc; do not touch this without running it.
class Sha256 {
 public:
  Sha256() = default;

  void Update(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
      buffer_[buffer_len_++] = data[i];
      if (buffer_len_ == 64) {
        Transform(buffer_);
        bit_len_ += 512;
        buffer_len_ = 0;
      }
    }
  }

  // Lowercase hex, 64 characters.
  std::string HexDigest() {
    size_t i = buffer_len_;
    if (buffer_len_ < 56) {
      buffer_[i++] = 0x80;
      while (i < 56) buffer_[i++] = 0x00;
    } else {
      buffer_[i++] = 0x80;
      while (i < 64) buffer_[i++] = 0x00;
      Transform(buffer_);
      memset(buffer_, 0, 56);
    }
    bit_len_ += buffer_len_ * 8;
    for (int j = 0; j < 8; j++) {
      buffer_[63 - j] = static_cast<uint8_t>(bit_len_ >> (8 * j));
    }
    Transform(buffer_);

    char out[65];
    for (int j = 0; j < 8; j++) {
      snprintf(out + j * 8, 9, "%08x", state_[j]);
    }
    return std::string(out, 64);
  }

 private:
  static uint32_t Rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
  }

  void Transform(const uint8_t* chunk) {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
        0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
      w[i] = (static_cast<uint32_t>(chunk[i * 4]) << 24) |
             (static_cast<uint32_t>(chunk[i * 4 + 1]) << 16) |
             (static_cast<uint32_t>(chunk[i * 4 + 2]) << 8) |
             static_cast<uint32_t>(chunk[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
      uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (int i = 0; i < 64; i++) {
      uint32_t s1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
      uint32_t ch = (e & f) ^ ((~e) & g);
      uint32_t t1 = h + s1 + ch + k[i] + w[i];
      uint32_t s0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
      uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t t2 = s0 + maj;
      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  uint32_t state_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  uint8_t buffer_[64] = {};
  size_t buffer_len_ = 0;
  uint64_t bit_len_ = 0;
};

std::string Sha256Hex(const uint8_t* data, size_t len) {
  Sha256 h;
  h.Update(data, len);
  return h.HexDigest();
}

ContainerStatus Fail(ContainerStatus status,
                     std::string* error,
                     const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
  return status;
}

}  // namespace

const char* StatusName(ContainerStatus status) {
  switch (status) {
    case ContainerStatus::kOk:
      return "ok";
    case ContainerStatus::kNotAContainer:
      return "not-a-container";
    case ContainerStatus::kUnsupportedVersion:
      return "unsupported-version";
    case ContainerStatus::kMalformed:
      return "malformed";
    case ContainerStatus::kWrongRelease:
      return "wrong-release";
    case ContainerStatus::kPayloadCorrupt:
      return "payload-corrupt";
  }
  return "unknown";
}

ContainerStatus SniffFile(const std::string& path) {
  if (!fml::IsFile(path)) {
    return ContainerStatus::kNotAContainer;
  }
  FILE* f = fopen(path.c_str(), "rb");
  if (f == nullptr) {
    // Unreadable is "not mine" rather than an error: whatever this is, the
    // Route B path cannot claim it, and the existing code path will report its
    // own failure with better context.
    return ContainerStatus::kNotAContainer;
  }
  uint8_t head[sizeof(kMagic)] = {};
  const size_t read = fread(head, 1, sizeof(head), f);
  fclose(f);
  if (read != sizeof(head) || memcmp(head, kMagic, sizeof(kMagic)) != 0) {
    return ContainerStatus::kNotAContainer;
  }
  return ContainerStatus::kOk;
}

ContainerStatus Parse(const uint8_t* bytes,
                      size_t length,
                      Container* out,
                      std::string* error) {
  if (bytes == nullptr || length < kFixedHeaderSize) {
    return Fail(ContainerStatus::kNotAContainer, error,
                "too short to be a patch container");
  }
  if (memcmp(bytes, kMagic, sizeof(kMagic)) != 0) {
    return Fail(ContainerStatus::kNotAContainer, error, "bad magic");
  }

  // Version before anything else that could be reinterpreted. A v2 container
  // may lay its header out differently, so parsing it with v1 rules would turn
  // a clean refusal into a malformed-file report.
  const uint32_t version = ReadU32LE(bytes + 8);
  if (version != kFormatVersion) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "unsupported format version %u (this build reads %u)", version,
             kFormatVersion);
    return Fail(ContainerStatus::kUnsupportedVersion, error, buf);
  }

  const uint32_t header_len = ReadU32LE(bytes + 12);
  if (kFixedHeaderSize + static_cast<uint64_t>(header_len) > length) {
    return Fail(ContainerStatus::kMalformed, error,
                "header runs past end of file");
  }

  const std::string header_json(
      reinterpret_cast<const char*>(bytes + kFixedHeaderSize), header_len);
  rapidjson::Document doc;
  doc.Parse(header_json.c_str(), header_json.size());
  if (doc.HasParseError() || !doc.IsObject()) {
    return Fail(ContainerStatus::kMalformed, error, "header is not JSON");
  }

  if (!doc.HasMember("release") || !doc["release"].IsObject() ||
      !doc["release"].HasMember("buildId") ||
      !doc["release"]["buildId"].IsString()) {
    return Fail(ContainerStatus::kMalformed, error,
                "header has no release.buildId");
  }
  if (!doc.HasMember("targets") || !doc["targets"].IsArray()) {
    return Fail(ContainerStatus::kMalformed, error, "header has no targets");
  }

  Container parsed;
  parsed.release_build_id = doc["release"]["buildId"].GetString();

  const size_t base = kFixedHeaderSize + header_len;
  for (const auto& entry : doc["targets"].GetArray()) {
    if (!entry.IsObject() || !entry.HasMember("library") ||
        !entry["library"].IsString() || !entry.HasMember("selector") ||
        !entry["selector"].IsString() || !entry.HasMember("offset") ||
        !entry["offset"].IsUint64() || !entry.HasMember("length") ||
        !entry["length"].IsUint64() || !entry.HasMember("sha256") ||
        !entry["sha256"].IsString()) {
      return Fail(ContainerStatus::kMalformed, error,
                  "target entry is missing required fields");
    }
    const uint64_t offset = entry["offset"].GetUint64();
    const uint64_t len = entry["length"].GetUint64();
    if (base + offset + len > length || base + offset < base) {
      return Fail(ContainerStatus::kMalformed, error,
                  "payload runs past end of file");
    }

    Target t;
    t.library = entry["library"].GetString();
    t.selector = entry["selector"].GetString();
    t.bytecode = bytes + base + offset;
    t.length = static_cast<size_t>(len);

    // Before anything is attached, not after: an apply can unwind attachments,
    // it cannot unwind a corrupt body already executing.
    const std::string actual = Sha256Hex(t.bytecode, t.length);
    if (actual != entry["sha256"].GetString()) {
      return Fail(ContainerStatus::kPayloadCorrupt, error,
                  "payload for " + t.selector + " is corrupt (declared " +
                      entry["sha256"].GetString() + ", got " + actual + ")");
    }
    parsed.targets.push_back(std::move(t));
  }

  if (parsed.targets.empty()) {
    // The producer refuses to write this, so seeing it means the file was
    // edited or truncated in a way the length checks did not catch.
    return Fail(ContainerStatus::kMalformed, error,
                "container declares no targets, so it would be silently inert");
  }

  if (out != nullptr) {
    *out = std::move(parsed);
  }
  return ContainerStatus::kOk;
}

}  // namespace route_b
}  // namespace flutter
