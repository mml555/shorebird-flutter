#include "flutter/shell/common/shorebird/shorebird.h"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "flutter/fml/file.h"
#include "flutter/fml/paths.h"
#include "flutter/shell/common/shorebird/updater.h"
#include "gtest/gtest.h"

namespace flutter {
namespace testing {
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

class LoadPatchOrBaseTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto mock = std::make_unique<shorebird::MockUpdater>();
    mock_ = mock.get();
    shorebird::Updater::SetInstanceForTesting(std::move(mock));
    shorebird::Updater::ResetLaunchStateForTesting();
  }

  void TearDown() override {
    mock_ = nullptr;
    shorebird::Updater::ResetInstanceForTesting();
    shorebird::Updater::ResetLaunchStateForTesting();
  }

  // Loads only the paths in |loadable|, recording every attempt.
  bool Load(const std::string& path) {
    attempts_.push_back(path);
    return std::find(loadable_.begin(), loadable_.end(), path) !=
           loadable_.end();
  }

  bool Run(const std::string& patch_path) {
    return LoadPatchOrBase(
        patch_path, "base.so",
        [this](const std::string& path) { return Load(path); });
  }

  shorebird::MockUpdater* mock_ = nullptr;
  std::vector<std::string> loadable_;
  std::vector<std::string> attempts_;
};

TEST_F(LoadPatchOrBaseTest, LoadsBaseWithoutAPatch) {
  loadable_ = {"base.so"};

  EXPECT_TRUE(Run(""));
  EXPECT_EQ(attempts_, std::vector<std::string>({"base.so"}));
  EXPECT_TRUE(mock_->call_log().empty());
}

TEST_F(LoadPatchOrBaseTest, LoadsPatchWithoutReporting) {
  loadable_ = {"patch.vmcode", "base.so"};

  EXPECT_TRUE(Run("patch.vmcode"));
  EXPECT_EQ(attempts_, std::vector<std::string>({"patch.vmcode"}));
  // The Shell reports the boot once it exists.
  EXPECT_TRUE(mock_->call_log().empty());
}

TEST_F(LoadPatchOrBaseTest, ReportsAFailedPatchAndLoadsBase) {
  loadable_ = {"base.so"};

  EXPECT_TRUE(Run("patch.vmcode"));
  EXPECT_EQ(attempts_, std::vector<std::string>({"patch.vmcode", "base.so"}));
  const auto& log = mock_->call_log();
#if SHOREBIRD_ROUTE_B
  // A Route B engine's PrepareNextBootPatch already recorded the start.
  ASSERT_EQ(log.size(), 1u);
  EXPECT_EQ(log[0], "ReportLaunchFailure");
#else
  ASSERT_EQ(log.size(), 2u);
  EXPECT_EQ(log[0], "ReportLaunchStart");
  EXPECT_EQ(log[1], "ReportLaunchFailure");
#endif
}

// The Shell that boots the base code reports start and success. Neither may
// reach the updater, or it would record the failed patch as booted.
TEST_F(LoadPatchOrBaseTest, BaseBootAfterAFailedPatchIsNotReportedAsSuccess) {
  loadable_ = {"base.so"};
  ASSERT_TRUE(Run("patch.vmcode"));

  shorebird::Updater::Instance().ReportLaunchStart();
  shorebird::Updater::Instance().ReportLaunchSuccess();

  EXPECT_EQ(mock_->launch_start_count(), 1);
  EXPECT_EQ(mock_->launch_failure_count(), 1);
  EXPECT_EQ(mock_->launch_success_count(), 0);
}

TEST_F(LoadPatchOrBaseTest, FailsWhenNeitherLoads) {
  EXPECT_FALSE(Run("patch.vmcode"));
  EXPECT_EQ(attempts_, std::vector<std::string>({"patch.vmcode", "base.so"}));
  EXPECT_EQ(mock_->launch_failure_count(), 1);
}

class ConfigureShorebirdTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto mock = std::make_unique<shorebird::MockUpdater>();
    mock_ = mock.get();
    shorebird::Updater::SetInstanceForTesting(std::move(mock));
    shorebird::Updater::ResetLaunchStateForTesting();
  }

  void TearDown() override {
    mock_ = nullptr;
    shorebird::Updater::ResetInstanceForTesting();
    shorebird::Updater::ResetLaunchStateForTesting();
  }

  static ShorebirdConfigArgs Args(std::string yaml,
                                  ReleaseVersion release_version) {
    return ShorebirdConfigArgs("cache", "storage", {"base.so"}, std::move(yaml),
                               std::move(release_version));
  }

  shorebird::MockUpdater* mock_ = nullptr;
};

TEST_F(ConfigureShorebirdTest, InitializesTheUpdaterAndReturnsThePatch) {
  mock_->set_next_boot_patch_path("patch.vmcode");
  std::string patch_path = "stale";

  EXPECT_TRUE(
      ConfigureShorebird(Args("app_id: abc\n", {"1.0.0", "3"}), patch_path));

  EXPECT_EQ(patch_path, "patch.vmcode");
  const auto& config = mock_->last_config();
  EXPECT_EQ(config.release_version, "1.0.0+3");
  EXPECT_EQ(config.original_libapp_paths,
            std::vector<std::string>({"base.so"}));
  EXPECT_EQ(config.yaml_config, "app_id: abc\n");
  // This host binary is a desktop build, whose directories every app of the
  // user shares, so the updater's are the app's own.
  EXPECT_EQ(config.code_cache_dir,
            fml::paths::JoinPaths({"cache", "shorebird_updater", "abc"}));
  EXPECT_EQ(config.app_storage_dir,
            fml::paths::JoinPaths({"storage", "shorebird_updater", "abc"}));
#if SHOREBIRD_ROUTE_B
  // One call validates, selects and attributes.
  EXPECT_EQ(mock_->prepare_count(), 1);
  EXPECT_EQ(mock_->validate_count(), 0);
#else
  EXPECT_EQ(mock_->validate_count(), 1);
  EXPECT_EQ(mock_->prepare_count(), 0);
#endif
  // The Shell reports the boot once it exists.
  EXPECT_EQ(mock_->launch_start_count(), 0);
}

TEST_F(ConfigureShorebirdTest, OmitsAnEmptyBuildNumber) {
  std::string patch_path;
  ConfigureShorebird(Args("app_id: abc\n", {"1.0.0", ""}), patch_path);
  EXPECT_EQ(mock_->last_config().release_version, "1.0.0");
}

TEST_F(ConfigureShorebirdTest, ClearsThePatchPathWithoutAPatch) {
  std::string patch_path = "stale";
  EXPECT_TRUE(
      ConfigureShorebird(Args("app_id: abc\n", {"1.0.0", "3"}), patch_path));
  EXPECT_EQ(patch_path, "");
}

TEST_F(ConfigureShorebirdTest, StopsWithoutAnAppId) {
  mock_->set_next_boot_patch_path("patch.vmcode");
  std::string patch_path = "stale";

  EXPECT_FALSE(
      ConfigureShorebird(Args("base_url: x\n", {"1.0.0", "3"}), patch_path));

  EXPECT_EQ(patch_path, "");
  EXPECT_TRUE(mock_->call_log().empty());
}

TEST(ApplyPatchToSettings, LeavesSettingsAloneWithoutAPatch) {
  Settings settings;
  settings.application_library_paths = {"libapp.so", "/data/app/libapp.so"};
  ApplyPatchToSettings("", settings);
  EXPECT_EQ(settings.application_library_paths,
            std::vector<std::string>({"libapp.so", "/data/app/libapp.so"}));
}

// This host binary runs without the interpreter, as Android does, where a
// patch replaces the release's library outright.
//
// The patch is a real file: a Route B engine reads it to tell a container from
// an ordinary code patch, and abstains when it cannot read it.
TEST(ApplyPatchToSettings, ReplacesTheLibraryWithThePatch) {
  fml::ScopedTemporaryDirectory dir;
  const std::string patch = fml::paths::JoinPaths({dir.path(), "patch.vmcode"});
  {
    std::ofstream out(patch, std::ios::binary);
    out << "an ordinary code patch";
  }
  Settings settings;
  settings.application_library_paths = {"libapp.so", "/data/app/libapp.so"};
  ApplyPatchToSettings(patch, settings);
  EXPECT_EQ(settings.application_library_paths,
            std::vector<std::string>({patch}));
}

// Desktop multi-window: a second engine in the same process gets
// AlreadyInitialized from the updater. It still boots the patch.
// https://github.com/shorebirdtech/shorebird/issues/3888
TEST_F(ConfigureShorebirdTest, SecondEngineStillBootsThePatch) {
  mock_->set_next_boot_patch_path("patch.vmcode");
  std::string first_path;
  ASSERT_TRUE(
      ConfigureShorebird(Args("app_id: abc\n", {"1.0.0", "3"}), first_path));

  mock_->set_init_result(false);
  std::string second_path;
  EXPECT_TRUE(
      ConfigureShorebird(Args("app_id: abc\n", {"1.0.0", "3"}), second_path));
  EXPECT_EQ(second_path, "patch.vmcode");
}

TEST_F(ConfigureShorebirdTest, FailsWhenNoInitSucceeded) {
  mock_->set_init_result(false);
  std::string patch_path;
  EXPECT_FALSE(
      ConfigureShorebird(Args("app_id: abc\n", {"1.0.0", "3"}), patch_path));
}

#if SHOREBIRD_ROUTE_B
// A second engine in one process must boot the patch the FIRST one did, even
// after the updater would name a newer one. Re-preparing would re-attribute the
// launch to a patch the first engine's snapshot never ran.
TEST_F(ConfigureShorebirdTest, SecondEngineGetsTheFirstEnginesPatch) {
  mock_->set_next_boot_patch_path("patches/1/dlc.vmcode");
  std::string first_path;
  ASSERT_TRUE(
      ConfigureShorebird(Args("app_id: abc\n", {"1.0.0", "3"}), first_path));

  mock_->set_next_boot_patch_path("patches/3/dlc.vmcode");
  mock_->set_init_result(false);
  std::string second_path;
  ConfigureShorebird(Args("app_id: abc\n", {"1.0.0", "3"}), second_path);

  EXPECT_EQ(second_path, "patches/1/dlc.vmcode");
  EXPECT_EQ(mock_->prepare_count(), 1);
}

// Route B activation arming contract. ConfigureShorebird cannot be reached for
// arming (it needs no snapshot here, but arming is deliberately not in it), so
// the unit is pinned directly.
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
  // The path is not read here: arming happens before the engine starts and the
  // container is opened later, from the callback. A missing file must still
  // arm, and fail (reporting) at activation time.
  EXPECT_NE(settings.root_isolate_create_callback, nullptr);
}

namespace {
// The hook ignores its isolate argument, so tests that run it on host, where
// no DartIsolate can be made, give it a reference that is never read.
const DartIsolate& NeverReadIsolate() {
  alignas(std::max_align_t) static char storage[1];
  return *reinterpret_cast<const DartIsolate*>(storage);
}

std::string WriteFile(const fml::ScopedTemporaryDirectory& dir,
                      const std::string& bytes) {
  const std::string path = fml::paths::JoinPaths({dir.path(), "dlc.vmcode"});
  std::ofstream out(path, std::ios::binary);
  out << bytes;
  return path;
}
}  // namespace

// Replacing rather than chaining would silently disable whatever the embedder
// installed, and the symptom would appear nowhere near Route B. Runs the
// installed callback (the path is unreadable, so it stops before any VM call)
// and checks the pre-existing one ran, once, and that Route B's own hook ran
// too (its abstain is visible in the updater).
TEST_F(ConfigureShorebirdTest, RouteBArmingChainsAnExistingCallback) {
  Settings settings;
  int existing_calls = 0;
  settings.root_isolate_create_callback = [&existing_calls](const auto&) {
    existing_calls++;
  };
  InstallRouteBActivationHook(settings, "/nonexistent/dlc.vmcode");
  ASSERT_NE(settings.root_isolate_create_callback, nullptr);
  EXPECT_EQ(existing_calls, 0) << "arming must not run it";

  settings.root_isolate_create_callback(NeverReadIsolate());
  EXPECT_EQ(existing_calls, 1);

  // Route B's hook ran after it: it abstained, so success is now dropped.
  shorebird::Updater::Instance().ReportLaunchSuccess();
  EXPECT_EQ(mock_->launch_success_count(), 0);
}

// S3: an unreadable artifact abstains. No failure (a transient read error must
// not tombstone a good patch) and no success (nothing was activated).
TEST_F(ConfigureShorebirdTest, HookAbstainsWhenTheArtifactIsUnreadable) {
  Settings settings;
  InstallRouteBActivationHook(settings, "/nonexistent/dlc.vmcode");
  settings.root_isolate_create_callback(NeverReadIsolate());
  shorebird::Updater::Instance().ReportLaunchSuccess();
  EXPECT_EQ(mock_->launch_failure_count(), 0);
  EXPECT_EQ(mock_->launch_success_count(), 0);
}

TEST_F(ConfigureShorebirdTest,
       ApplyPatchToSettingsAbstainsOnAnUnreadablePatch) {
  Settings settings;
  settings.application_library_paths = {"libapp.so"};
  ApplyPatchToSettings("/nonexistent/dlc.vmcode", settings);
  EXPECT_EQ(settings.root_isolate_create_callback, nullptr);
  EXPECT_EQ(settings.application_library_paths,
            std::vector<std::string>({"libapp.so"}));
  shorebird::Updater::Instance().ReportLaunchSuccess();
  shorebird::Updater::Instance().ReportLaunchFailure();
  EXPECT_EQ(mock_->launch_failure_count(), 0);
  EXPECT_EQ(mock_->launch_success_count(), 0);
}

// S2: a container the hook refuses reports FAILURE before Dart starts, so the
// success Dart reports later is dropped and the patch is never promoted.
TEST_F(ConfigureShorebirdTest, HookReportsFailureForARefusedContainer) {
  fml::ScopedTemporaryDirectory dir;
  // Right magic, version 0: refused as an unsupported version.
  const std::string path = WriteFile(dir, "SBRBPTCH" + std::string(16, '\0'));
  Settings settings;
  InstallRouteBActivationHook(settings, path);
  settings.root_isolate_create_callback(NeverReadIsolate());

  EXPECT_EQ(mock_->launch_failure_count(), 1);
  shorebird::Updater::Instance().ReportLaunchSuccess();
  EXPECT_EQ(mock_->launch_success_count(), 0);
}

TEST(Shorebird, OnlyAFullyAppliedContainerIsAPatch) {
  EXPECT_TRUE(RouteBFullyApplied(3, 3));
  EXPECT_FALSE(RouteBFullyApplied(3, 2)) << "partial";
  EXPECT_FALSE(RouteBFullyApplied(3, 0)) << "applied 0/N";
  EXPECT_FALSE(RouteBFullyApplied(0, 0)) << "an empty container is inert";
}

TEST(Shorebird, AssertPoolOffsetSidecarIsOneInteger) {
  int64_t v = -1;
  EXPECT_TRUE(ParseAssertPoolOffset("0xd4a8\n", &v));
  EXPECT_EQ(v, 0xd4a8);
  EXPECT_TRUE(ParseAssertPoolOffset("1234", &v));
  EXPECT_EQ(v, 1234);

  v = -1;
  EXPECT_FALSE(ParseAssertPoolOffset("0x1zzz", &v));
  EXPECT_FALSE(ParseAssertPoolOffset("", &v));
  EXPECT_FALSE(ParseAssertPoolOffset("nope", &v));
  EXPECT_FALSE(ParseAssertPoolOffset("99999999999999999999999", &v));
  EXPECT_EQ(v, -1) << "left untouched on failure";
}

// A Route B container is bytes plus a header, not a snapshot. Handed to the VM
// as the app's library it is a failure to boot, so it must never reach
// application_library_paths, whatever the file is called.
TEST(ApplyPatchToSettings, ArmsARouteBContainerAndLeavesTheLibraryPathsAlone) {
  fml::ScopedTemporaryDirectory dir;
  const std::string path = fml::paths::JoinPaths({dir.path(), "dlc.vmcode"});
  {
    std::ofstream out(path, std::ios::binary);
    out << "SBRBPTCH" << std::string(16, '\0');
  }
  Settings settings;
  settings.application_library_paths = {"libapp.so"};

  ApplyPatchToSettings(path, settings);

  EXPECT_EQ(settings.application_library_paths,
            std::vector<std::string>({"libapp.so"}));
  EXPECT_NE(settings.root_isolate_create_callback, nullptr);
}

// The G15 two-engine case: arming is a property of the patch path alone. A
// second engine's Init fails by design, and nothing about that may skip it.
TEST_F(ConfigureShorebirdTest, SecondEngineIsArmedEvenThoughItsInitFailed) {
  fml::ScopedTemporaryDirectory dir;
  const std::string path = fml::paths::JoinPaths({dir.path(), "dlc.vmcode"});
  {
    std::ofstream out(path, std::ios::binary);
    out << "SBRBPTCH" << std::string(16, '\0');
  }
  mock_->set_next_boot_patch_path(path);
  std::string first_path;
  ASSERT_TRUE(
      ConfigureShorebird(Args("app_id: abc\n", {"1.0.0", "3"}), first_path));

  mock_->set_init_result(false);
  std::string second_path;
  ConfigureShorebird(Args("app_id: abc\n", {"1.0.0", "3"}), second_path);

  Settings second;
  ApplyPatchToSettings(second_path, second);
  EXPECT_NE(second.root_isolate_create_callback, nullptr);
}

// Anything that is not a container is an ordinary code patch and takes the
// platform's usual path (here, a desktop host: it replaces the library).
TEST(ApplyPatchToSettings, LeavesANonContainerToThePlatformPath) {
  fml::ScopedTemporaryDirectory dir;
  const std::string path = WriteFile(dir, "an ordinary code patch");
  Settings settings;
  settings.application_library_paths = {"libapp.so"};
  ApplyPatchToSettings(path, settings);
  EXPECT_EQ(settings.root_isolate_create_callback, nullptr);
}
#endif  // SHOREBIRD_ROUTE_B

}  // namespace testing
}  // namespace flutter