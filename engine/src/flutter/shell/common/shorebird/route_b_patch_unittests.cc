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

// sha256("hello") — used so the fixtures below carry a real digest rather than
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
         R"("selector":"greet","offset":0,"length":)" +
         std::to_string(length) + R"(,"sha256":")" + sha + R"("}]})";
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

// The SHA-256 in route_b_patch.cc is hand-written because no shell target
// depends on boringssl. This is the guard on that decision: if this fails,
// every payload integrity check in the file is meaningless.
TEST(RouteBPatchTest, Sha256MatchesKnownVector) {
  // Exercised through the only public surface that hashes: a container whose
  // declared digest is the known sha256 of "hello" must parse clean.
  const auto bytes = BuildContainer(GoodHeader(), "hello");
  EXPECT_EQ(ParseVec(bytes), ContainerStatus::kOk);

  // ...and the same bytes with any other declared digest must not.
  const auto wrong = BuildContainer(
      GoodHeader("abc123", std::string(64, '0')), "hello");
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
  const auto bytes = BuildContainer(
      R"({"formatVersion":1,"targets":[]})", "hello");
  EXPECT_EQ(ParseVec(bytes), ContainerStatus::kMalformed);
}

TEST(RouteBPatchTest, PayloadRunsPastEndOfFile) {
  // Declares 5000 bytes of payload, ships 5.
  const auto bytes = BuildContainer(GoodHeader("abc123", kHelloSha256, 5000),
                                    "hello");
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
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(
                            container.targets[0].bytecode),
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
      ContainerStatus::kOk,          ContainerStatus::kNotAContainer,
      ContainerStatus::kUnsupportedVersion, ContainerStatus::kMalformed,
      ContainerStatus::kWrongRelease, ContainerStatus::kPayloadCorrupt};
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

TEST(RouteBPatchTest, SniffMissingFileIsNotAContainer) {
  EXPECT_EQ(route_b::SniffFile("/definitely/not/here/dlc.vmcode"),
            ContainerStatus::kNotAContainer);
}

}  // namespace testing
}  // namespace flutter
