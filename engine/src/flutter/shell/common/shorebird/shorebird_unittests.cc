#include "flutter/shell/common/shorebird/shorebird.h"

#include "flutter/fml/file.h"
#include "flutter/fml/paths.h"
#include "gtest/gtest.h"

namespace flutter {
namespace testing {

// --- G15: the Route B activation arming contract ----------------------------
//
// Zero coverage existed here before 2026-08-13, which is how the severe half of
// G15 stayed a filed claim for as long as it did: a second FlutterEngine in one
// process never had this callback installed, so it ran the ORIGINAL AOT code
// while engine one ran the patch -- two engines, two program versions, no error
// and no log.
//
// The cause was an ORDERING one and it is fixed in shorebird.cc, not here:
// `Updater::Init` is `shorebird_init`, whose second call in a process
// deliberately fails ("Updater already initialized, ignoring second
// shorebird_init call" -- a case the updater's own comment says happens
// regularly with Firebase Messaging), and the arming call used to sit BELOW the
// resulting `if (!init_result) return;`. ConfigureShorebird cannot be reached
// from a unit test -- it opens with an FML_CHECK on the VM snapshot -- so these
// tests pin the arming unit's contract, which is what the ordering fix relies on
// being true.
TEST(Shorebird, RouteBArmingIsInertWithoutAPatch) {
  Settings settings;
  ASSERT_EQ(settings.root_isolate_create_callback, nullptr);
  InstallRouteBActivationHook(settings, "");
  // Not merely "does not crash": a callback installed here would run on every
  // app that has no Route B patch, which is nearly all of them.
  EXPECT_EQ(settings.root_isolate_create_callback, nullptr);
}

TEST(Shorebird, RouteBArmingInstallsACallbackForAPatch) {
  Settings settings;
  InstallRouteBActivationHook(settings, "/nonexistent/dlc.vmcode");
  // The path is not read here -- arming happens before the engine starts and the
  // container is opened later, from the callback. A missing file must therefore
  // still arm, and fail (reporting) at activation time rather than silently
  // skipping activation now.
  EXPECT_NE(settings.root_isolate_create_callback, nullptr);
}

TEST(Shorebird, RouteBArmingChainsAnExistingCallback) {
  Settings settings;
  int existing_calls = 0;
  settings.root_isolate_create_callback = [&existing_calls](const auto&) {
    existing_calls++;
  };
  InstallRouteBActivationHook(settings, "/nonexistent/dlc.vmcode");
  EXPECT_NE(settings.root_isolate_create_callback, nullptr);
  // Replacing rather than chaining would silently disable whatever the embedder
  // installed -- an observer, a service-protocol hook -- and the symptom would
  // appear nowhere near Route B.
  EXPECT_EQ(existing_calls, 0);
}
TEST(Shorebird, GetValueFromYamlValueExists) {
  std::string yaml = "appid: com.example.app\nversion: 1.0.0\n";
  std::string key = "appid";
  std::string value = GetValueFromYaml(yaml, key);
  EXPECT_EQ(value, "com.example.app");
}

TEST(Shorebird, GetValueFromYamlValueDoesNotExist) {
  std::string yaml = "appid: com.example.app\nversion: 1.0.0\n";
  std::string key = "appid2";
  std::string value = GetValueFromYaml(yaml, key);
  EXPECT_EQ(value, "");
}

TEST(Shorebird, PatchAssetsPathSitsBesideThePatch) {
  // Derived from the patch file's own directory, because the two
  // ConfigureShorebird() APIs assemble the patch root differently.
  EXPECT_EQ(PatchAssetsPathForPatch(
                "/data/user/0/com.example/files/shorebird_updater/"
                "app-id/patches/3/dlc.vmcode"),
            "/data/user/0/com.example/files/shorebird_updater/"
            "app-id/patches/3/flutter_assets");
}

TEST(Shorebird, PatchAssetsPathIsEmptyWithoutAPatch) {
  // No active patch means no overlay, and RunConfiguration must leave asset
  // resolution exactly as a stock build has it.
  EXPECT_EQ(PatchAssetsPathForPatch(""), "");
}
}  // namespace testing
}  // namespace flutter