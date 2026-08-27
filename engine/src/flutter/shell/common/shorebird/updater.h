// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_COMMON_SHOREBIRD_UPDATER_H_
#define FLUTTER_SHELL_COMMON_SHOREBIRD_UPDATER_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace flutter {
namespace shorebird {

/// File callbacks for iOS patch loading.
/// Mirrors the FileCallbacks struct from the Rust updater.
struct FileCallbacks {
  void* (*open)(void);
  uintptr_t (*read)(void* file_handle, uint8_t* buffer, uintptr_t count);
  int64_t (*seek)(void* file_handle, int64_t offset, int32_t whence);
  void (*close)(void* file_handle);
};

/// Configuration for initializing the Shorebird updater.
struct AppConfig {
  /// Version string for this release (e.g., "1.0.0+1").
  std::string release_version;

  /// Paths to the original AOT libraries (libapp.so on Android, App.framework
  /// on iOS).
  std::vector<std::string> original_libapp_paths;

  /// Directory for persistent updater state (survives app updates).
  std::string app_storage_dir;

  /// Directory for cached artifacts (cleared on app updates).
  std::string code_cache_dir;

  /// Callbacks for iOS patch file access (can be null callbacks on Android).
  FileCallbacks file_callbacks;

  /// YAML configuration from shorebird.yaml.
  std::string yaml_config;
};

/// Abstract interface for the Shorebird updater.
///
/// This abstraction allows for:
/// 1. Mocking in tests without requiring the real Rust library
/// 2. Future migration from Rust to C++ implementation
/// 3. Test instrumentation (call counting, logging)
///
/// ## Boot preparation is ONE operation, not a sequence
///
/// `PrepareNextBootPatch()` validates the candidate, tombstones it if it is
/// unusable, selects what will actually run, records that selection as the
/// booting patch, and returns its path — all inside a single updater state
/// transition. The invariant it exists to hold:
///
///   the patch recorded as `currently_booting` == the patch whose path is
///   returned here == the patch the VM is handed
///
/// This REPLACES the former three-call sequence `ValidateNextBootPatch()` →
/// `NextBootPatchPath()` → `ReportLaunchStart()`, which could not hold that
/// invariant because the three steps read the candidate pointer at three
/// different times. On iOS they were interleaved in the worst possible order:
/// `SetBaseSnapshot()` resolves the base isolate snapshot, which reached
/// `ResolveIsolateData()` in runtime/dart_snapshot.cc and reported launch start
/// one line BEFORE `ValidateNextBootPatch()` ran. A patch rejected for a bad
/// signature was therefore correctly refused execution while remaining credited
/// as the booting patch; the next `ReportLaunchSuccess()` promoted it, and
/// `cleanup_older_than` then deleted the last-known-good patch that had really
/// booted, dropping the following launch to the base release.
///
/// Measured on device, with the log excerpt, in
/// `selfhost/evidence/p6-signing/ARM_C_EXECUTION_IDENTITY.md`. There was no
/// security defect — the rejected bytes never executed — but the bookkeeping
/// destroyed the fallback the refusal exists to preserve. The single-call shape
/// is what makes that class of disagreement unrepresentable rather than merely
/// unlikely.
///
/// The old two accessors are deliberately GONE from this interface rather than
/// left in place unused: while they exist, a future caller can reassemble the
/// broken ordering without noticing.
///
/// ## Once per process
///
/// Preparation and success/failure reporting are each guarded to run at most
/// once per process, because:
/// 1. The Rust updater is a process-global singleton — preparing twice would
///    re-read `next_boot` and could promote a newly-downloaded (but not yet
///    booted) patch even though the running engine loaded the old snapshot.
/// 2. In add-to-app, multiple FlutterEngines may be created and destroyed in
///    one process. Each creation resolves snapshots and constructs a Shell, but
///    only the first actually boots.
///
/// A second `PrepareNextBootPatch()` call therefore does NOT re-prepare: it
/// returns the path the first call returned, cached. Returning an empty string
/// instead would silently downgrade a later engine to the base release, and
/// re-preparing would break the invariant above. Both halves matter, so both
/// are pinned by test.
///
/// Tests can call `ResetLaunchStateForTesting()` to re-enable the guards.
class Updater {
 public:
  virtual ~Updater() = default;

  /// Initialize the updater with configuration.
  /// @param config Configuration containing release version, paths, and
  /// callbacks
  /// @return true if initialization succeeded
  virtual bool Init(const AppConfig& config) = 0;

  /// Prepare the next boot and return the path the VM must ACTUALLY execute.
  ///
  /// Validation, selection and launch attribution happen together; see the
  /// class comment. A rejected candidate yields the last-known-good patch, and
  /// that is a success, not an error.
  ///
  /// Guarded to run at most once per process: a second call returns the first
  /// call's path without touching updater state.
  ///
  /// @return Path to the patch to boot, or empty string for the base release.
  std::string PrepareNextBootPatch();

  // Boot lifecycle methods — guarded to run at most once per process.
  // Callers may call these freely; subsequent calls after the first are
  // silently ignored.
  void ReportLaunchSuccess();
  void ReportLaunchFailure();

  // Update checking
  virtual bool ShouldAutoUpdate() = 0;
  virtual void StartUpdateThread() = 0;

  // Singleton access
  static Updater& Instance();

  // Test support - allows injecting a mock implementation
  static void SetInstanceForTesting(std::unique_ptr<Updater> instance);
  static void ResetInstanceForTesting();

  /// Resets the once-per-process launch guards so tests can verify
  /// start/success/failure calls on fresh Updater instances.
  static void ResetLaunchStateForTesting();

 protected:
  Updater() = default;

  // Subclass hooks — called by the public guarded methods above.
  virtual std::string DoPrepareNextBootPatch() = 0;
  virtual void DoReportLaunchSuccess() = 0;
  virtual void DoReportLaunchFailure() = 0;

 private:
  static std::unique_ptr<Updater> instance_;
  static std::mutex instance_mutex_;

  // Once-per-process guards for the launch lifecycle. `prepare_mutex_` also
  // protects `prepared_path_`, so a second engine racing the first observes the
  // finished result rather than an empty string mid-preparation.
  static std::mutex prepare_mutex_;
  static bool boot_prepared_;
  static std::string prepared_path_;
  static std::atomic<bool> launch_completed_;
};

/// No-op implementation for unsupported platforms.
/// All methods are safe to call but do nothing.
class NoOpUpdater : public Updater {
 public:
  NoOpUpdater() = default;
  ~NoOpUpdater() override = default;

  bool Init(const AppConfig& config) override { return true; }
  std::string DoPrepareNextBootPatch() override { return ""; }
  void DoReportLaunchSuccess() override {}
  void DoReportLaunchFailure() override {}
  bool ShouldAutoUpdate() override { return false; }
  void StartUpdateThread() override {}
};

#if SHOREBIRD_PLATFORM_SUPPORTED
/// Production implementation that wraps the Rust updater C API.
/// Only available on supported platforms (Android, iOS, macOS, Windows, Linux).
class RealUpdater : public Updater {
 public:
  RealUpdater() = default;
  ~RealUpdater() override = default;

  bool Init(const AppConfig& config) override;
  std::string DoPrepareNextBootPatch() override;
  void DoReportLaunchSuccess() override;
  void DoReportLaunchFailure() override;
  bool ShouldAutoUpdate() override;
  void StartUpdateThread() override;
};
#endif  // SHOREBIRD_PLATFORM_SUPPORTED

/// Mock implementation for testing.
/// Tracks call counts and can be queried to verify behavior.
class MockUpdater : public Updater {
 public:
  MockUpdater() = default;
  ~MockUpdater() override = default;

  bool Init(const AppConfig& config) override;
  std::string DoPrepareNextBootPatch() override;
  void DoReportLaunchSuccess() override;
  void DoReportLaunchFailure() override;
  bool ShouldAutoUpdate() override;
  void StartUpdateThread() override;

  // Test accessors
  int init_count() const { return init_count_; }
  int prepare_count() const { return prepare_count_; }
  int launch_success_count() const { return launch_success_count_; }
  int launch_failure_count() const { return launch_failure_count_; }
  int start_update_thread_count() const { return start_update_thread_count_; }
  const std::vector<std::string>& call_log() const { return call_log_; }

  // Last init parameters (for verification)
  const std::string& last_release_version() const {
    return last_release_version_;
  }
  const std::string& last_yaml_config() const { return last_yaml_config_; }

  // Test configuration
  void set_init_result(bool value) { init_result_ = value; }
  void set_should_auto_update(bool value) { should_auto_update_ = value; }
  void set_next_boot_patch_path(const std::string& path) {
    next_boot_patch_path_ = path;
  }

  // Reset all counters and logs
  void Reset();

 private:
  int init_count_ = 0;
  int prepare_count_ = 0;
  int launch_success_count_ = 0;
  int launch_failure_count_ = 0;
  int start_update_thread_count_ = 0;
  bool init_result_ = true;
  bool should_auto_update_ = false;
  std::string next_boot_patch_path_;
  std::string last_release_version_;
  std::string last_yaml_config_;
  std::vector<std::string> call_log_;
};

}  // namespace shorebird
}  // namespace flutter

#endif  // FLUTTER_SHELL_COMMON_SHOREBIRD_UPDATER_H_
