#ifndef FLUTTER_SHELL_COMMON_SHOREBIRD_SHOREBIRD_H_
#define FLUTTER_SHELL_COMMON_SHOREBIRD_SHOREBIRD_H_

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
/// Used by Desktop implementations.
struct ShorebirdConfigArgs {
  std::string code_cache_path;
  std::string app_storage_path;
  std::string release_app_library_path;
  std::string shorebird_yaml;
  ReleaseVersion release_version;

  ShorebirdConfigArgs(std::string code_cache_path,
                      std::string app_storage_path,
                      std::string release_app_library_path,
                      std::string shorebird_yaml,
                      ReleaseVersion release_version)
      : code_cache_path(code_cache_path),
        app_storage_path(app_storage_path),
        release_app_library_path(release_app_library_path),
        shorebird_yaml(shorebird_yaml),
        release_version(release_version) {}
};

/// Newer api, used by Desktop implementations.
/// Does not directly manipulate Settings.
bool ConfigureShorebird(const ShorebirdConfigArgs& args,
                        std::string& patch_path);

/// Older api used by iOS and Android, directly manipulates Settings.
void ConfigureShorebird(std::string code_cache_path,
                        std::string app_storage_path,
                        Settings& settings,
                        const std::string& shorebird_yaml,
                        const std::string& version,
                        const std::string& version_code);

/// Used for reading app_id from shorebird.yaml.
/// Exposed for testing.
std::string GetValueFromYaml(const std::string& yaml, const std::string& key);

/// Arms Route B activation on `settings`: installs a root_isolate_create_callback
/// that applies the container at `path` once the root isolate exists.
///
/// Inert when `path` is empty -- the overwhelmingly common case is no Route B
/// patch at all, and it must cost nothing. CHAINS any existing callback rather
/// than replacing one.
///
/// Exposed for testing. `ConfigureShorebird` itself is not directly unit-testable:
/// it opens with FML_CHECK(DartSnapshot::VMSnapshotFromSettings(settings)), which
/// aborts in a test binary with no AOT snapshot linked, which is why nothing tests
/// it today. So the arming CONTRACT is pinned here. G15's ordering requirement --
/// arm BEFORE the `!init_result` early return, so a second engine in one process
/// is armed too -- rests on the argument recorded at that call site, not on these
/// tests.
void InstallRouteBActivationHook(Settings& settings, const std::string& path);

/// The directory holding the `flutter_assets` overlay shipped alongside an
/// active patch, given that patch's own file path, or "" when
/// `active_patch_path` is empty.
///
/// Derived from the patch file's directory rather than recomputed from
/// app_storage_path: the two ConfigureShorebird() APIs assemble that path
/// differently (one appends the app id, one does not), and the updater is the
/// only authority on where the patch actually landed.
///
/// Returns a path whether or not it exists. Callers hand it to
/// DirectoryAssetBundle, and AssetManager::PushFront already rejects a resolver
/// over a missing directory, so a patch without assets needs no separate check.
///
/// Exposed so desktop embedders, which use the ConfigureShorebird() overload
/// that does not touch Settings, can populate
/// Settings::shorebird_patch_assets_path themselves.
std::string PatchAssetsPathForPatch(const std::string& active_patch_path);

}  // namespace flutter

#endif  // FLUTTER_SHELL_COMMON_SHOREBIRD_SHOREBIRD_H_
