// Copyright (c) 2026, the Shorebird self-host fork.
//
// The rejection taxonomy, asserted. Every one of these outcomes has a different
// cause and a different fix, and the reason they are separate codes at all is
// that Route B kept paying for the collapsed version: a stale payload reported
// as "attach returned false" sent us looking at retention, bytecode metadata
// and the installer in turn, none of which were wrong.

#include "flutter/shell/common/shorebird/route_b_patch.h"

#include <string>
#include <vector>

#include "flutter/testing/testing.h"

namespace flutter {
namespace testing {

namespace {

using route_b::Container;
using route_b::ContainerStatus;

void AppendU32LE(std::vector<uint8_t>* out, uint32_t v) {
  out->push_back(static_cast<uint8_t>(v & 0xff));
  out->push_back(static_cast<uint8_t>((v >> 8) & 0xff));
  out->push_back(static_cast<uint8_t>((v >> 16) & 0xff));
  out->push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}

// sha256("hello"), computed with `printf hello | shasum -a 256` (not a NIST
// vector) — used so the fixtures below carry a real digest rather than
// a placeholder the reader would have to be taught to ignore.
constexpr char kHelloSha256[] =
    "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824";

std::vector<uint8_t> BuildContainer(const std::string& header_json,
                                    const std::string& payload,
                                    uint32_t version = 1) {
  std::vector<uint8_t> out;
  const char magic[] = {'S', 'B', 'R', 'B', 'P', 'T', 'C', 'H'};
  out.insert(out.end(), magic, magic + 8);
  AppendU32LE(&out, version);
  AppendU32LE(&out, static_cast<uint32_t>(header_json.size()));
  out.insert(out.end(), header_json.begin(), header_json.end());
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

std::string GoodHeader(const std::string& build_id = "abc123",
                       const std::string& sha = kHelloSha256,
                       size_t length = 5) {
  return std::string(R"({"formatVersion":1,"release":{"buildId":")") +
         build_id + R"("},"targets":[{"library":"package:a/a.dart",)" +
         R"("selector":"greet","offset":0,"length":)" + std::to_string(length) +
         R"(,"sha256":")" + sha + R"("}]})";
}

ContainerStatus ParseVec(const std::vector<uint8_t>& bytes,
                         Container* out = nullptr,
                         std::string* error = nullptr) {
  std::string scratch_error;
  Container scratch;
  return route_b::Parse(bytes.data(), bytes.size(),
                        out != nullptr ? out : &scratch,
                        error != nullptr ? error : &scratch_error);
}

}  // namespace

std::string Sha(const std::string& s) {
  return route_b::Sha256Hex(reinterpret_cast<const uint8_t*>(s.data()),
                            s.size());
}

// The SHA-256 in route_b_patch.cc is hand-written because no shell target
// depends on boringssl. This is the guard on that decision: if this fails,
// every payload integrity check in the file is meaningless.
//
// The first three are the FIPS 180-2 / NIST example vectors. The two-block one
// (448 bits) is the case where the length does not fit in the first block's
// padding.
TEST(RouteBPatchTest, Sha256NistVectors) {
  EXPECT_EQ(Sha(""),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(Sha("abc"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(Sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  EXPECT_EQ(Sha(std::string(1000000, 'a')),
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// Padding is where hand-rolled SHA-256 breaks: the length goes into the last 8
// bytes of a block, so 55 bytes fit one block and 56 and above spill into a
// second. Digests are from `printf %Ns | shasum -a 256` for N a's.
TEST(RouteBPatchTest, Sha256CrossesThePaddingBoundary) {
  EXPECT_EQ(Sha(std::string(55, 'a')),
            "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
  EXPECT_EQ(Sha(std::string(56, 'a')),
            "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
  EXPECT_EQ(Sha(std::string(64, 'a')),
            "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
}

// The same hash through the public parse surface, so a container whose
// declared digest is right parses and any other does not.
TEST(RouteBPatchTest, PayloadDigestIsChecked) {
  const auto bytes = BuildContainer(GoodHeader(), "hello");
  EXPECT_EQ(ParseVec(bytes), ContainerStatus::kOk);
  const auto wrong =
      BuildContainer(GoodHeader("abc123", std::string(64, '0')), "hello");
  EXPECT_EQ(ParseVec(wrong), ContainerStatus::kPayloadCorrupt);
}

TEST(RouteBPatchTest, NotAContainer) {
  // An ordinary code patch. This is the common case and must be cheap and
  // silent, not an error.
  const std::string vmcode = "\x28\xb5\x2f\xfd not a route b container";
  const std::vector<uint8_t> bytes(vmcode.begin(), vmcode.end());
  EXPECT_EQ(ParseVec(bytes), ContainerStatus::kNotAContainer);
}

TEST(RouteBPatchTest, TooShortIsNotAContainer) {
  const std::vector<uint8_t> bytes = {'S', 'B', 'R'};
  EXPECT_EQ(ParseVec(bytes), ContainerStatus::kNotAContainer);
}

TEST(RouteBPatchTest, UnsupportedVersion) {
  const auto bytes = BuildContainer(GoodHeader(), "hello", /*version=*/2);
  std::string error;
  EXPECT_EQ(ParseVec(bytes, nullptr, &error),
            ContainerStatus::kUnsupportedVersion);
  // Refused on the version alone: a v2 container may lay its header out
  // differently, so parsing it with v1 rules would report "malformed" for a
  // file that is perfectly well formed.
  EXPECT_NE(error.find("version 2"), std::string::npos);
}

TEST(RouteBPatchTest, MalformedHeaderJson) {
  const auto bytes = BuildContainer("{not json", "hello");
  EXPECT_EQ(ParseVec(bytes), ContainerStatus::kMalformed);
}

TEST(RouteBPatchTest, MissingReleaseBuildId) {
  const auto bytes =
      BuildContainer(R"({"formatVersion":1,"targets":[]})", "hello");
  EXPECT_EQ(ParseVec(bytes), ContainerStatus::kMalformed);
}

TEST(RouteBPatchTest, PayloadRunsPastEndOfFile) {
  // Declares 5000 bytes of payload, ships 5.
  const auto bytes =
      BuildContainer(GoodHeader("abc123", kHelloSha256, 5000), "hello");
  EXPECT_EQ(ParseVec(bytes), ContainerStatus::kMalformed);
}

TEST(RouteBPatchTest, HeaderRunsPastEndOfFile) {
  auto bytes = BuildContainer(GoodHeader(), "hello");
  // Overwrite headerLen with something enormous.
  bytes[12] = 0xff;
  bytes[13] = 0xff;
  EXPECT_EQ(ParseVec(bytes), ContainerStatus::kMalformed);
}

TEST(RouteBPatchTest, NoTargetsIsMalformed) {
  const auto bytes = BuildContainer(
      R"({"formatVersion":1,"release":{"buildId":"abc"},"targets":[]})",
      "hello");
  // A container with no targets would apply cleanly and do nothing, which is
  // the worst possible outcome: a patch that reports success and changes
  // nothing.
  EXPECT_EQ(ParseVec(bytes), ContainerStatus::kMalformed);
}

TEST(RouteBPatchTest, ParsesTargets) {
  Container container;
  const auto bytes = BuildContainer(GoodHeader("deadbeef"), "hello");
  ASSERT_EQ(ParseVec(bytes, &container), ContainerStatus::kOk);
  EXPECT_EQ(container.release_build_id, "deadbeef");
  ASSERT_EQ(container.targets.size(), 1u);
  EXPECT_EQ(container.targets[0].library, "package:a/a.dart");
  EXPECT_EQ(container.targets[0].selector, "greet");
  EXPECT_EQ(container.targets[0].length, 5u);
  EXPECT_EQ(
      std::string(reinterpret_cast<const char*>(container.targets[0].bytecode),
                  container.targets[0].length),
      "hello");
}

// Release identity is NOT checked here: it needs a live isolate, so the hook
// compares release_build_id itself. What this asserts is that the parser hands
// the caller the identity to compare, before any target is resolved — which is
// the ordering that keeps a stale payload from degrading into a target or
// attach failure.
TEST(RouteBPatchTest, ReleaseIdentityIsAvailableBeforeTargetsAreUsed) {
  Container container;
  const auto bytes = BuildContainer(GoodHeader("11223344"), "hello");
  ASSERT_EQ(ParseVec(bytes, &container), ContainerStatus::kOk);
  EXPECT_EQ(container.release_build_id, "11223344");
}

TEST(RouteBPatchTest, StatusNamesAreDistinct) {
  // A code nobody can read is a bool with extra steps.
  const std::vector<ContainerStatus> all = {
      ContainerStatus::kOk,
      ContainerStatus::kNotAContainer,
      ContainerStatus::kUnsupportedVersion,
      ContainerStatus::kMalformed,
      ContainerStatus::kWrongRelease,
      ContainerStatus::kPayloadCorrupt,
      ContainerStatus::kUnreadable};
  std::vector<std::string> names;
  for (const auto status : all) {
    names.emplace_back(route_b::StatusName(status));
  }
  for (size_t i = 0; i < names.size(); i++) {
    EXPECT_FALSE(names[i].empty());
    EXPECT_NE(names[i], "unknown");
    for (size_t j = i + 1; j < names.size(); j++) {
      EXPECT_NE(names[i], names[j]);
    }
  }
}

// Unreadable is NOT "not a container": it is no verdict at all, and callers
// abstain on it instead of reporting a failure.
TEST(RouteBPatchTest, SniffMissingFileIsUnreadable) {
  EXPECT_EQ(route_b::SniffFile("/definitely/not/here/dlc.vmcode"),
            ContainerStatus::kUnreadable);
}

// --- Bounds (B1) -----------------------------------------------------------

std::string Entry(const std::string& selector,
                  const std::string& offset,
                  const std::string& length,
                  const std::string& sha) {
  return R"({"library":"package:a/a.dart","selector":")" + selector +
         R"(","offset":)" + offset + R"(,"length":)" + length +
         R"(,"sha256":")" + sha + R"("})";
}

std::string Header(const std::vector<std::string>& entries) {
  std::string t;
  for (size_t i = 0; i < entries.size(); i++) {
    t += (i ? "," : "") + entries[i];
  }
  return R"({"formatVersion":1,"release":{"buildId":"abc"},"targets":[)" + t +
         "]}";
}

// 16 + header_len is the payload base. A length of 2^64 - base + k wraps
// `base + offset + len` to k, which used to pass the bounds check and then read
// the whole address space.
TEST(RouteBPatchTest, LengthThatWrapsTheSumIsMalformed) {
  // Every length below has 20 digits, so the header (and so `base`) is the same
  // size for all of them; compute it from a 20-digit placeholder.
  const std::string placeholder(20, '9');
  const uint64_t base =
      16 + Header({Entry("f", "0", placeholder, kHelloSha256)}).size();
  for (const uint64_t k : {0, 1, 3, 5, 100}) {
    // base + 0 + len == k (mod 2^64), which is <= the file length.
    const uint64_t len = (UINT64_MAX - base + 1) + k;
    const auto header =
        Header({Entry("f", "0", std::to_string(len), kHelloSha256)});
    ASSERT_EQ(16 + header.size(), base) << "len=" << len;
    EXPECT_EQ(ParseVec(BuildContainer(header, "hello")),
              ContainerStatus::kMalformed)
        << "len=" << len;
  }
  const auto max_len =
      Header({Entry("f", "0", std::to_string(UINT64_MAX), kHelloSha256)});
  EXPECT_EQ(ParseVec(BuildContainer(max_len, "hello")),
            ContainerStatus::kMalformed);
}

TEST(RouteBPatchTest, OffsetNearTwoToTheSixtyFourIsMalformed) {
  for (const uint64_t offset : {UINT64_MAX, UINT64_MAX - 1, UINT64_MAX - 20}) {
    const auto header =
        Header({Entry("f", std::to_string(offset), "5", kHelloSha256)});
    EXPECT_EQ(ParseVec(BuildContainer(header, "hello")),
              ContainerStatus::kMalformed)
        << "offset=" << offset;
  }
}

TEST(RouteBPatchTest, OffsetAndLengthThatOnlyWrapTogetherAreMalformed) {
  // Each is individually small enough to look fine; their sum is not.
  const auto header = Header(
      {Entry("f", "9223372036854775808", "9223372036854775808", kHelloSha256)});
  EXPECT_EQ(ParseVec(BuildContainer(header, "hello")),
            ContainerStatus::kMalformed);
}

// "world" -> sha256 below; the payload is "hello" + "world".
constexpr char kWorldSha256[] =
    "486ea46224d1bb4fb680f34f7c9ad96a8f24ec88be73ea8e5a6c65260e9cb8a7";

TEST(RouteBPatchTest, MultipleTargetsAtNonZeroOffsets) {
  Container container;
  const auto header = Header({Entry("first", "0", "5", kHelloSha256),
                              Entry("second", "5", "5", kWorldSha256)});
  // Kept alive: a Container's targets point into the bytes it was parsed from.
  const auto bytes = BuildContainer(header, "helloworld");
  ASSERT_EQ(ParseVec(bytes, &container), ContainerStatus::kOk);
  ASSERT_EQ(container.targets.size(), 2u);
  EXPECT_EQ(
      std::string(reinterpret_cast<const char*>(container.targets[0].bytecode),
                  container.targets[0].length),
      "hello");
  EXPECT_EQ(
      std::string(reinterpret_cast<const char*>(container.targets[1].bytecode),
                  container.targets[1].length),
      "world");
}

TEST(RouteBPatchTest, TargetsMayBeOutOfOrderInThePayload) {
  Container container;
  const auto header = Header({Entry("second", "5", "5", kWorldSha256),
                              Entry("first", "0", "5", kHelloSha256)});
  const auto bytes = BuildContainer(header, "helloworld");
  ASSERT_EQ(ParseVec(bytes, &container), ContainerStatus::kOk);
  EXPECT_EQ(container.targets[0].selector, "second");
  EXPECT_EQ(container.targets[1].selector, "first");
}

TEST(RouteBPatchTest, TargetRunningExactlyToEndOfFileIsAccepted) {
  const auto header = Header({Entry("w", "5", "5", kWorldSha256)});
  EXPECT_EQ(ParseVec(BuildContainer(header, "helloworld")),
            ContainerStatus::kOk);
}

TEST(RouteBPatchTest, TargetRunningOneBytePastEndOfFileIsMalformed) {
  const auto header = Header({Entry("w", "5", "6", kWorldSha256)});
  EXPECT_EQ(ParseVec(BuildContainer(header, "helloworld")),
            ContainerStatus::kMalformed);
  // And an offset one byte past the end, with an empty payload.
  const auto at_eof = Header({Entry("w", "10", "0", kWorldSha256)});
  EXPECT_EQ(ParseVec(BuildContainer(at_eof, "helloworld")),
            ContainerStatus::kPayloadCorrupt);  // in bounds, wrong digest
  const auto past = Header({Entry("w", "11", "0", kWorldSha256)});
  EXPECT_EQ(ParseVec(BuildContainer(past, "helloworld")),
            ContainerStatus::kMalformed);
}

// --- Header limits (S7) ----------------------------------------------------

TEST(RouteBPatchTest, DeeplyNestedHeaderDoesNotExhaustTheStack) {
  // 900k open brackets: under the header cap, far past any recursive parser's
  // stack. It is not a valid header, so the answer is "malformed", and the
  // point is that we get an answer.
  const std::string header(900 * 1000, '[');
  std::string error;
  EXPECT_EQ(ParseVec(BuildContainer(header, "hello"), nullptr, &error),
            ContainerStatus::kMalformed);
}

TEST(RouteBPatchTest, OversizeHeaderIsRefusedBeforeItIsCopied) {
  // Declares a 4 GiB - 1 header. Whatever the file holds, it must not be
  // copied into a string and parsed.
  auto bytes = BuildContainer(GoodHeader(), "hello");
  bytes[12] = bytes[13] = bytes[14] = bytes[15] = 0xff;
  std::string error;
  EXPECT_EQ(ParseVec(bytes, nullptr, &error), ContainerStatus::kMalformed);
  EXPECT_NE(error.find("too large"), std::string::npos) << error;

  // One byte over the 1 MiB cap, with a file big enough to hold it.
  const std::string big(1024 * 1024 + 1, ' ');
  EXPECT_NE(ParseVec(BuildContainer(big, "hello"), nullptr, &error),
            ContainerStatus::kOk);
  EXPECT_NE(error.find("too large"), std::string::npos) << error;
}

}  // namespace testing
}  // namespace flutter
