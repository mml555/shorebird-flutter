// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/common/shorebird/updater.h"

#include <string>
#include <thread>
#include <vector>

#include "gtest/gtest.h"

namespace flutter {
namespace shorebird {
namespace testing {

class UpdaterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Install a mock for each test and reset the once-per-process guards
    // so each test starts with a clean slate.
    auto mock = std::make_unique<MockUpdater>();
    mock_ = mock.get();
    Updater::SetInstanceForTesting(std::move(mock));
    Updater::ResetLaunchStateForTesting();
  }

  void TearDown() override {
    mock_ = nullptr;
    Updater::ResetInstanceForTesting();
    Updater::ResetLaunchStateForTesting();
  }

  MockUpdater* mock_ = nullptr;
};

// PrepareNextBootPatch reaches the updater exactly once per process...
TEST_F(UpdaterTest, PrepareNextBootPatchOnlyPreparesOnce) {
  mock_->set_next_boot_patch_path("/patches/1/dlc.vmcode");
  EXPECT_EQ(mock_->prepare_count(), 0);

  EXPECT_EQ(Updater::Instance().PrepareNextBootPatch(),
            "/patches/1/dlc.vmcode");
  EXPECT_EQ(mock_->prepare_count(), 1);

  Updater::Instance().PrepareNextBootPatch();
  EXPECT_EQ(mock_->prepare_count(), 1);
}

// ...and the second caller must get the FIRST call's answer. Returning an empty
// string here would silently downgrade a second engine to the base release,
// which is why this is asserted separately from the call count above.
TEST_F(UpdaterTest, SecondPrepareReturnsTheSamePathWithoutRepreparing) {
  mock_->set_next_boot_patch_path("/patches/1/dlc.vmcode");
  const std::string first = Updater::Instance().PrepareNextBootPatch();

  // A background update lands and the updater would now hand out patch 3.
  mock_->set_next_boot_patch_path("/patches/3/dlc.vmcode");

  const std::string second = Updater::Instance().PrepareNextBootPatch();
  EXPECT_EQ(second, first);
  EXPECT_EQ(second, "/patches/1/dlc.vmcode");
  EXPECT_EQ(mock_->prepare_count(), 1)
      << "a later engine must not re-attribute the launch to a newer patch";
}

// The base-release case must be cached as a real answer, not mistaken for
// "not yet prepared" -- otherwise every engine re-enters the updater.
TEST_F(UpdaterTest, EmptyPathIsStillARealPreparedResult) {
  mock_->set_next_boot_patch_path("");
  EXPECT_EQ(Updater::Instance().PrepareNextBootPatch(), "");
  EXPECT_EQ(Updater::Instance().PrepareNextBootPatch(), "");
  EXPECT_EQ(mock_->prepare_count(), 1);
}

// Concurrent engine creation must not produce two preparations, and must not
// let either caller observe a half-finished result.
TEST_F(UpdaterTest, ConcurrentPrepareYieldsOnePreparationAndOneAnswer) {
  mock_->set_next_boot_patch_path("/patches/1/dlc.vmcode");
  constexpr int kThreads = 8;
  std::vector<std::string> results(kThreads);
  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int i = 0; i < kThreads; i++) {
    threads.emplace_back([&results, i] {
      results[i] = Updater::Instance().PrepareNextBootPatch();
    });
  }
  for (auto& t : threads) {
    t.join();
  }
  EXPECT_EQ(mock_->prepare_count(), 1);
  for (int i = 0; i < kThreads; i++) {
    EXPECT_EQ(results[i], "/patches/1/dlc.vmcode") << "thread " << i;
  }
}

TEST_F(UpdaterTest, ReportLaunchSuccessOnlyCallsOnce) {
  EXPECT_EQ(mock_->launch_success_count(), 0);

  Updater::Instance().ReportLaunchSuccess();
  EXPECT_EQ(mock_->launch_success_count(), 1);

  // Second call is a no-op.
  Updater::Instance().ReportLaunchSuccess();
  EXPECT_EQ(mock_->launch_success_count(), 1);
}

TEST_F(UpdaterTest, ReportLaunchFailureOnlyCallsOnce) {
  EXPECT_EQ(mock_->launch_failure_count(), 0);

  Updater::Instance().ReportLaunchFailure();
  EXPECT_EQ(mock_->launch_failure_count(), 1);

  // Second call is a no-op.
  Updater::Instance().ReportLaunchFailure();
  EXPECT_EQ(mock_->launch_failure_count(), 1);
}

TEST_F(UpdaterTest, MockUpdaterTracksShouldAutoUpdate) {
  mock_->set_should_auto_update(false);
  EXPECT_FALSE(Updater::Instance().ShouldAutoUpdate());

  mock_->set_should_auto_update(true);
  EXPECT_TRUE(Updater::Instance().ShouldAutoUpdate());
}

TEST_F(UpdaterTest, MockUpdaterTracksStartUpdateThreadCalls) {
  EXPECT_EQ(mock_->start_update_thread_count(), 0);

  Updater::Instance().StartUpdateThread();
  EXPECT_EQ(mock_->start_update_thread_count(), 1);
}

// The boot sequence a production launch performs, in order. There is no
// separate launch-start step to get out of order any more -- that is the point.
TEST_F(UpdaterTest, MockUpdaterCallLogRecordsSequence) {
  EXPECT_TRUE(mock_->call_log().empty());

  Updater::Instance().PrepareNextBootPatch();
  Updater::Instance().ShouldAutoUpdate();
  Updater::Instance().ReportLaunchSuccess();

  const auto& log = mock_->call_log();
  ASSERT_EQ(log.size(), 3u);
  EXPECT_EQ(log[0], "PrepareNextBootPatch");
  EXPECT_EQ(log[1], "ShouldAutoUpdate");
  EXPECT_EQ(log[2], "ReportLaunchSuccess");
}

TEST_F(UpdaterTest, MockUpdaterResetClearsState) {
  Updater::Instance().PrepareNextBootPatch();
  Updater::Instance().ReportLaunchSuccess();
  mock_->set_should_auto_update(true);

  EXPECT_EQ(mock_->prepare_count(), 1);
  EXPECT_EQ(mock_->launch_success_count(), 1);
  EXPECT_TRUE(mock_->ShouldAutoUpdate());

  mock_->Reset();

  EXPECT_EQ(mock_->prepare_count(), 0);
  EXPECT_EQ(mock_->launch_success_count(), 0);
  // Check call_log before ShouldAutoUpdate() since the method adds to call_log
  EXPECT_TRUE(mock_->call_log().empty());
  EXPECT_FALSE(mock_->ShouldAutoUpdate());
}

// Preparation and success reporting are paired once per process.
TEST_F(UpdaterTest, PrepareAndSuccessArePairedOncePerProcess) {
  Updater::Instance().PrepareNextBootPatch();
  Updater::Instance().ReportLaunchSuccess();

  EXPECT_EQ(mock_->prepare_count(), 1);
  EXPECT_EQ(mock_->launch_success_count(), 1);
  const auto& log = mock_->call_log();
  ASSERT_EQ(log.size(), 2u);
  EXPECT_EQ(log[0], "PrepareNextBootPatch");
  EXPECT_EQ(log[1], "ReportLaunchSuccess");
}

TEST_F(UpdaterTest, PrepareAndFailureArePairedOncePerProcess) {
  Updater::Instance().PrepareNextBootPatch();
  Updater::Instance().ReportLaunchFailure();

  EXPECT_EQ(mock_->prepare_count(), 1);
  EXPECT_EQ(mock_->launch_failure_count(), 1);
  const auto& log = mock_->call_log();
  ASSERT_EQ(log.size(), 2u);
  EXPECT_EQ(log[0], "PrepareNextBootPatch");
  EXPECT_EQ(log[1], "ReportLaunchFailure");
}

// The add-to-app scenario: multiple engines run the boot sequence, but only the
// first reaches the updater. This prevents promoting a newly-downloaded patch
// to "current_boot" while subsequent engines still run the original snapshot.
TEST_F(UpdaterTest, MultipleEnginesOnlyReportOnce) {
  // First engine boots.
  Updater::Instance().PrepareNextBootPatch();
  Updater::Instance().ReportLaunchSuccess();

  // Second engine boots — these should be no-ops.
  Updater::Instance().PrepareNextBootPatch();
  Updater::Instance().ReportLaunchSuccess();

  EXPECT_EQ(mock_->prepare_count(), 1);
  EXPECT_EQ(mock_->launch_success_count(), 1);

  const auto& log = mock_->call_log();
  ASSERT_EQ(log.size(), 2u);
  EXPECT_EQ(log[0], "PrepareNextBootPatch");
  EXPECT_EQ(log[1], "ReportLaunchSuccess");
}

// ResetLaunchStateForTesting re-enables the guards, allowing tests to
// verify launch calls on a fresh state.
TEST_F(UpdaterTest, ResetLaunchStateReenablesGuards) {
  Updater::Instance().PrepareNextBootPatch();
  Updater::Instance().ReportLaunchSuccess();
  EXPECT_EQ(mock_->prepare_count(), 1);
  EXPECT_EQ(mock_->launch_success_count(), 1);

  Updater::ResetLaunchStateForTesting();

  Updater::Instance().PrepareNextBootPatch();
  Updater::Instance().ReportLaunchSuccess();
  EXPECT_EQ(mock_->prepare_count(), 2);
  EXPECT_EQ(mock_->launch_success_count(), 2);
}

}  // namespace testing
}  // namespace shorebird
}  // namespace flutter
