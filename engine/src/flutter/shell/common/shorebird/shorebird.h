#ifndef FLUTTER_SHELL_COMMON_SHOREBIRD_SHOREBIRD_H_
#define FLUTTER_SHELL_COMMON_SHOREBIRD_SHOREBIRD_H_

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "flutter/common/settings.h"
#include "flutter/fml/memory/ref_ptr.h"
#include "shell/platform/embedder/embedder.h"

namespace flutter {

class DartSnapshot;

/// Version and build number of the release.
/// Used by ShorebirdConfigArgs.
struct ReleaseVersion {
  std::string version;
  std::string build_number;
};

/// Arguments for ConfigureShorebird.
struct ShorebirdConfigArgs {
  /// Where the updater caches downloaded patches.
  std::string code_cache_path;
  /// Where the updater keeps state that outlives a release, such as its
  /// client id.
  std::string app_storage_path;
  /// The release's own AOT library, as the platform finds it. Android and iOS
  /// pass every candidate path in Settings::application_library_paths.
  std::vector<std::string> release_app_library_paths;
  std::string shorebird_yaml;
  ReleaseVersion release_version;

  ShorebirdConfigArgs(std::string code_cache_path,
                      std::string app_storage_path,
                      std::vector<std::string> release_app_library_paths,
                      std::string shorebird_yaml,
                      ReleaseVersion release_version)
      : code_cache_path(std::move(code_cache_path)),
        app_storage_path(std::move(app_storage_path)),
        release_app_library_paths(std::move(release_app_library_paths)),
        shorebird_yaml(std::move(shorebird_yaml)),
        release_version(std::move(release_version)) {}
};

/// Initializes the updater and picks the patch to boot. Every platform calls
/// this. Sets |patch_path| to that patch, or clears it when there is none.
/// Returns whether the updater initialized in this call.
///
/// Desktop embedders load |patch_path| with LoadPatchOrBase. Android and iOS
/// hand it to the Shell with ApplyPatchToSettings.
bool ConfigureShorebird(const ShorebirdConfigArgs& args,
                        std::string& patch_path);

/// Loads the AOT code for this boot with |load|, which returns whether it
/// loaded the library at the path it is given. Tries |patch_path| first,
/// unless it is empty. If the patch does not load, reports the launch as
/// failed, so the updater marks the patch bad and sends a failure event, and
/// loads |base_path|, the release's own code, instead. Returns whether the
/// last load succeeded. Used by the desktop embedders, which load the AOT
/// library themselves before any Shell exists.
bool LoadPatchOrBase(const std::string& patch_path,
                     const std::string& base_path,
                     const std::function<bool(const std::string&)>& load);

/// Points |settings| at |patch_path|, unless it is empty, so the Shell boots
/// the patch. Used by Android and iOS, whose Shell resolves the AOT code from
/// Settings::application_library_paths.
///
/// In a `shorebird_route_b` engine a Route B container is not a library: it
/// is left out of the library paths and armed instead, see
/// InstallRouteBActivationHook.
void ApplyPatchToSettings(const std::string& patch_path, Settings& settings);

/// Used for reading app_id from shorebird.yaml.
/// Exposed for testing.
std::string GetValueFromYaml(const std::string& yaml, const std::string& key);

#if SHOREBIRD_ROUTE_B
/// Arms Route B activation on |settings|: installs a root_isolate_create_callback
/// that applies the container at |path| once the root isolate exists, after its
/// libraries load and before main runs.
///
/// Inert when |path| is empty, which is nearly every launch. CHAINS an existing
/// callback rather than replacing it. The container is not read here: arming
/// happens before the engine starts, so a missing file still arms and is
/// refused (and reported) at activation time.
///
/// Exposed for testing.
void InstallRouteBActivationHook(Settings& settings, const std::string& path);
#endif  // SHOREBIRD_ROUTE_B

}  // namespace flutter

#endif  // FLUTTER_SHELL_COMMON_SHOREBIRD_SHOREBIRD_H_
