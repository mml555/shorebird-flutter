
#include "flutter/shell/common/shorebird/shorebird.h"

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "flutter/fml/command_line.h"
#include "flutter/fml/file.h"
#include "flutter/fml/macros.h"
#include "flutter/fml/mapping.h"
#include "flutter/fml/message_loop.h"
#include "flutter/fml/native_library.h"
#include "flutter/fml/paths.h"
#include "flutter/lib/ui/plugins/callback_cache.h"
#include "flutter/runtime/dart_snapshot.h"
#include "flutter/runtime/dart_vm.h"
#include "flutter/shell/common/shell.h"
#include "flutter/shell/common/shorebird/route_b_patch.h"
#include "flutter/shell/common/shorebird/snapshots_data_handle.h"
#include "flutter/shell/common/shorebird/updater.h"
#include "flutter/shell/common/switches.h"
#include "fml/logging.h"
#include "shell/platform/embedder/embedder.h"
#include "third_party/dart/runtime/include/dart_api.h"
#include "third_party/dart/runtime/include/dart_native_api.h"
#include "third_party/dart/runtime/include/dart_route_b_trace.h"
#include "third_party/dart/runtime/include/dart_tools_api.h"

// Namespaced to avoid Google style warnings.
namespace flutter {

// Old Android versions (e.g. the v16 ndk Flutter uses) don't always include a
// getauxval symbol, but the Rust ring crate assumes it exists:
// https://github.com/briansmith/ring/blob/fa25bf3a7403c9fe6458cb87bd8427be41225ca2/src/cpu/arm.rs#L22
// It uses it to determine if the CPU supports AES instructions.
// Making this a weak symbol allows the linker to use a real version instead
// if it can find one.
// BoringSSL just reads from procfs instead, which is what we would do if
// we needed to implement this ourselves.  Implementation looks straightforward:
// https://lwn.net/Articles/519085/
// https://github.com/google/boringssl/blob/6ab4f0ae7f2db96d240eb61a5a8b4724e5a09b2f/crypto/cpu_arm_linux.c
#if defined(__ANDROID__) && defined(__arm__)
extern "C" __attribute__((weak)) unsigned long getauxval(unsigned long type) {
  return 0;
}
#endif

// Global reference to the base (unpatched) snapshot from the App.framework,
// set on iOS and macOS. Patches there are diffed against just the Dart
// snapshot regions of the app binary, which FileCallbacksImpl serves to the
// updater from this snapshot. It is process-global because:
// 1. The Shorebird updater library is a process-global singleton with its own
//    internal state.
// 2. The base snapshot is immutable (baked into the app) so sharing it across
//    isolate groups is safe.
//
// Note: This design doesn't support multiple engines with different base
// snapshots, but I'm not aware of any use cases for that.
static fml::RefPtr<const DartSnapshot> vm_snapshot;

#if SHOREBIRD_USE_INTERPRETER
// Resolving walks settings.application_library_paths with dlopen, so the
// returned DartSnapshot owns a reference to App.framework. Where nothing else
// holds the image, that reference is the only one: a standalone Runner carries
// no LC_LOAD_DYLIB on App.framework, unlike an add-to-app host. Resolving
// twice in a row then unmaps the image and re-maps it from disk, because the
// first snapshot is destroyed before the second resolve runs.
//
// That is also why the crash is the first engine's only. Once vm_snapshot
// below is set it holds the image for the process, so a later resolve-and-drop
// has nothing left to unmap.
static fml::RefPtr<const DartSnapshot> ResolveBaseVMSnapshot(
    const Settings& settings) {
  // If you are crashing here, you probably are running Shorebird in a Debug
  // config, where the AOT snapshot won't be linked into the process, and thus
  // lookups will fail.  Change your Scheme to Release to fix:
  // https://github.com/flutter/flutter/wiki/Debugging-the-engine#debugging-ios-builds-with-xcode
  auto snapshot = DartSnapshot::VMSnapshotFromSettings(settings);
  FML_CHECK(snapshot) << "XCode Scheme must be set to Release to use Shorebird";
  return snapshot;
}

void SetBaseSnapshot(fml::RefPtr<const DartSnapshot> base_vm_snapshot) {
  // Only the VM snapshot, which reports nothing to the updater. Resolving the
  // isolate snapshot (IsolateSnapshotFromSettings) reports the launch start,
  // and this runs whenever a FlutterDartProject is created: before the next
  // boot patch is validated, and whether or not an engine ever runs. A start
  // reported here would mark the patch as booting for an engine that may never
  // run, so the next launch would mark a good patch bad, and it would name a
  // patch that validation may go on to reject. The start belongs to the Shell
  // (ResolveIsolateData), as on Android. The VM and isolate symbols resolve
  // the same buffer, so the VM snapshot is the base app snapshot. Holding it
  // here keeps App.framework mapped; see ResolveBaseVMSnapshot.
  vm_snapshot = std::move(base_vm_snapshot);
  // Shorebird_SetBaseSnapshots below hands the interpreter both blobs, while
  // DartSnapshot::IsValid() checks only the data half, so a snapshot can be
  // non-null with no instructions at all. Check what is read, not what is
  // returned. vm_snapshot is already known non-null: ResolveBaseVMSnapshot
  // checked it before handing it over.
  FML_CHECK(vm_snapshot->IsValidForAOT())
      << "Shorebird base snapshot resolved without its instructions mapping";

  // Per-mapping observability for the base snapshot. Logged at every app boot
  // so customer syslogs include the exact sizes the on-device base reader
  // exposes — directly comparable to the host's `aot_tools dump_blobs`
  // extraction the patch was generated against.
  const uint8_t* data_ptr = vm_snapshot->GetDataMapping();
  const uint8_t* insns_ptr = vm_snapshot->GetInstructionsMapping();
  intptr_t data_size = data_ptr ? Dart_SnapshotDataSize(data_ptr) : -1;
  intptr_t insns_size = insns_ptr ? Dart_SnapshotInstrSize(insns_ptr) : -1;
  FML_LOG(INFO) << "[shorebird] SetBaseSnapshot mappings: "
                << "data_size=" << data_size << " insns_size=" << insns_size
                << " total=" << (data_size + insns_size);

  // The VM isolate snapshot is gone, so there are no separate VM buffers to
  // hand over. Shorebird_SetBaseSnapshots ignores them, as the Dart
  // embedder's own call in runtime/bin/main_impl.cc does.
  Shorebird_SetBaseSnapshots(data_ptr, insns_ptr,
                             /*base_vm_snapshot_data=*/nullptr,
                             /*base_vm_snapshot_instructions=*/nullptr);
}
#endif  // SHOREBIRD_USE_INTERPRETER

class FileCallbacksImpl {
 public:
  static void* Open();
  static uintptr_t Read(void* file, uint8_t* buffer, uintptr_t length);
  static int64_t Seek(void* file, int64_t offset, int32_t whence);
  static void Close(void* file);
};

shorebird::FileCallbacks ShorebirdFileCallbacks() {
  return {
      .open = FileCallbacksImpl::Open,
      .read = FileCallbacksImpl::Read,
      .seek = FileCallbacksImpl::Seek,
      .close = FileCallbacksImpl::Close,
  };
}

// Given the contents of a yaml file, return the given value if it exists,
// otherwise return an empty string.
// Does not support nested keys.
std::string GetValueFromYaml(const std::string& yaml, const std::string& key) {
  std::stringstream ss(yaml);
  std::string line;
  std::string prefix = key + ":";
  while (std::getline(ss, line, '\n')) {
    if (line.find(prefix) != std::string::npos) {
      auto ret = line.substr(line.find(prefix) + prefix.size());

      // Remove leading and trailing spaces
      while (!ret.empty() && std::isspace(ret.front())) {
        ret.erase(0, 1);
      }
      while (!ret.empty() && std::isspace(ret.back())) {
        ret.pop_back();
      }
      return ret;
    }
  }
  return "";
}

#if SHOREBIRD_ROUTE_B
// The supplied pool offset of the negative control; see the call site.
constexpr int64_t kDefaultAssertPoolOffset = 0xd4a8;

// The pre-main hook's outcome, written next to the artifact.
//
// WHY A FILE. The taxonomy exists so a refusal names its own cause, and on this
// rig that is worthless without a channel: no engine log line of ANY kind
// reaches idevicesyslog for a --noninteractive launch, Flutter's own included,
// because stderr is not routed there. The first 4b device run refused and left
// nothing to read -- exactly the failure the taxonomy was built to prevent, one
// level up.
//
// Written beside the installed artifact, inside the app's own container, so
// `ios-deploy --download` can retrieve it. Best-effort: a diagnostic that can
// fail the boot it is diagnosing is worse than no diagnostic.
void RouteBReport(const std::string& artifact_path, const std::string& line) {
  FML_LOG(ERROR) << "ROUTEB: " << line;
  const std::string report_path = artifact_path + ".routeb";
  FILE* f = fopen(report_path.c_str(), "a");
  if (f == nullptr) {
    return;
  }
  fprintf(f, "%s\n", line.c_str());
  fclose(f);
}

// The post-attach trace, in its OWN file.
//
// Deliberately not appended to `.routeb`. That file carries byte-for-byte
// committed evidence (`selfhost/engine/route_b/evidence/`) and ABSENCE
// semantics other probes read as "never armed", so adding lines to it would put
// this diagnostic in tension with both. A sibling path is discovered the same
// way by `ios-deploy --download` and costs nothing.
//
// No `FML_LOG` mirror either: device logs are not reliably capturable, which is
// the whole reason this is a file, and duplicating it there invites reading the
// log instead of the record.
void RouteBTraceWrite(const std::string& artifact_path,
                      const std::string& line) {
  const std::string trace_path = artifact_path + ".routeb.trace";
  FILE* f = fopen(trace_path.c_str(), "a");
  if (f == nullptr) {
    return;
  }
  fprintf(f, "%s\n", line.c_str());
  fclose(f);
}

// Pure, so it is unit-testable on host with a hand-filled struct — no device,
// no engine, no patch.
//
// EVERY FIELD IS PRINTED, including the ones that agree with expectation. The
// lesson of `applied 1/1` is that a summary hides the thing you did not think
// to ask about, so this records values and leaves the conclusion to the reader.
std::string FormatRouteBTrace(const Dart_RouteBTrace& t,
                              const std::string& library,
                              const std::string& selector,
                              size_t payload_length) {
  std::ostringstream o;
  // v=5 adds the derived target->pool fields. The version is bumped rather than
  // the fields quietly appended, because a reader that assumes v4's field set
  // would take `tpool_*`'s absence for a measurement.
  o << "rbtrace v=5"
    << " lib=" << library << " sel=" << selector << " paylen=" << payload_length
    << " rc=" << t.result << " attach_entered=" << t.attach_entered
    << " attach_returned=" << t.attach_returned
    << " bc_pre=" << t.has_bytecode_pre << " bc_post=" << t.has_bytecode_post
    << " interp_pre=" << t.is_interpreted_pre
    << " interp_post=" << t.is_interpreted_post
    << " bc_size=" << t.bytecode_size << " code_pre_size=" << t.code_pre_size
    << " uep_post_is_interpret_call=" << t.entry_point_post_is_interpret_call
    << " caller_scan_status=" << t.caller_scan_status
    << " pool_status=" << t.pool_status << " pool_index=" << t.pool_index
    << " pool_length=" << t.pool_length
    << " pool_entry_is_function=" << t.pool_entry_is_function
    << " pool_entry_equals_target=" << t.pool_entry_equals_target
    << " caller_resolved=" << t.caller_resolved
    << " caller_pool_fns=" << t.caller_pool_functions << " caller_pool_matches="
    << t.caller_pool_matches_target
    // DERIVED target->pool identity. `tpool_matches` is authoritative: index
    // and offset mean an identity ONLY at tpool_status=4 (UNIQUE). At 5
    // (AMBIGUOUS) they are the FIRST of several and `tpool_index2` is the next,
    // so the ambiguity is in the record instead of being resolved by whoever
    // reads it.
    << " tpool_status=" << t.target_pool_status
    << " tpool_matches=" << t.target_pool_matches
    << " tpool_index=" << t.target_pool_index
    << " tpool_index2=" << t.target_pool_index_second
    << " tpool_scanned=" << t.target_pool_scanned;
  // TWO LAYERS, LABELLED AS SUCH. v1 printed Code values under `uep_*` and the
  // resulting mismatch read as a genuine VM anomaly for an entire
  // investigation.
  o << std::hex << std::showbase << " fn=" << t.function_ptr
    << " fn_ep_pre=" << t.fn_entry_point_pre
    << " fn_ep_post=" << t.fn_entry_point_post
    << " fn_uep_pre=" << t.fn_unchecked_entry_point_pre
    << " fn_uep_post=" << t.fn_unchecked_entry_point_post
    << " code_pre=" << t.code_pre << " code_post=" << t.code_post
    << " code_ep_pre=" << t.code_entry_point_pre
    << " code_ep_post=" << t.code_entry_point_post
    << " code_uep_pre=" << t.code_unchecked_entry_point_pre
    << " code_uep_post=" << t.code_unchecked_entry_point_post
    << " interpret_call_ep=" << t.interpret_call_entry_point
    << " caller_pool_other_fn=" << t.caller_pool_other_fn
    << " pool_offset=" << t.pool_offset
    << " tpool_offset=" << t.target_pool_offset
    << " pool_entry_ptr=" << t.pool_entry_ptr;
  return o.str();
}

// Route B (selfhost), SEAM 6: activate a patch after the root isolate exists
// and before main runs.
//
// The lifecycle keeps deciding WHICH patch is active; the engine only decides
// HOW a Route B patch becomes live. That ownership split is why this is a
// separate hook armed from ApplyPatchToSettings rather than more code inside
// ConfigureShorebird, and it is why the path handed in here is the one
// PrepareNextBootPatch() already chose.
//
// TIMING. root_isolate_create_callback fires at dart_isolate.cc:163, inside a
// tonic::DartState::Scope: after the isolate exists and its libraries are
// loaded, and before RunFromLibrary/InvokeMainEntrypoint at :174. Route B needs
// live Function objects so it cannot run earlier, and must precede user Dart so
// it cannot run later. That one-line window is the whole seam.
//
// FAIL CLOSED. Every refusal leaves the program pristine and returns, so the
// app boots on unpatched AOT. A Route B patch that cannot be applied must never
// be the difference between running and not running.
//
// Existing callback is CHAINED, never replaced: root_isolate_create_callback is
// a general extension point (fuchsia and the embedder API both set it), and
// silently consuming another subsystem's hook would be a nasty regression to
// debug.
void InstallRouteBActivationHook(Settings& settings, const std::string& path) {
  if (path.empty()) {
    // Inert. The overwhelmingly common case is no Route B patch at all, and it
    // must cost nothing.
    return;
  }
  auto existing = settings.root_isolate_create_callback;

  settings.root_isolate_create_callback = [existing,
                                           path](const DartIsolate& isolate) {
    if (existing) {
      existing(isolate);
    }
    RouteBReport(path, "hook entered");

    auto mapping = fml::FileMapping::CreateReadOnly(path);
    if (!mapping || mapping->GetSize() == 0) {
      RouteBReport(path, "REFUSED (unreadable)");
      return;
    }

    route_b::Container container;
    std::string error;
    const auto status = route_b::Parse(mapping->GetMapping(),
                                       mapping->GetSize(), &container, &error);
    if (status != route_b::ContainerStatus::kOk) {
      RouteBReport(path, std::string("REFUSED (") +
                             route_b::StatusName(status) + "): " + error);
      return;
    }
    RouteBReport(path,
                 "parsed, targets=" + std::to_string(container.targets.size()) +
                     ", built-for=" + container.release_build_id);

    // RELEASE IDENTITY BEFORE TARGET RESOLUTION. Patch bytecode is compiled
    // against one release's kernel, so applying it elsewhere is undefined
    // rather than merely unsupported. Checking it here means a stale
    // payload says "wrong release" instead of degrading into a target or
    // attach failure three steps later -- which is the single most
    // expensive diagnostic confusion this project has had.
    char* running = Dart_RouteBReleaseBuildId();
    if (running == nullptr) {
      // No identity is not "any identity". Refuse.
      RouteBReport(path,
                   "REFUSED (no-release-identity): this snapshot carries "
                   "no build ID");
      return;
    }
    const std::string running_id(running);
    ::free(running);
    RouteBReport(path, "running=" + running_id);
    if (running_id != container.release_build_id) {
      RouteBReport(path, std::string("REFUSED (") +
                             route_b::StatusName(
                                 route_b::ContainerStatus::kWrongRelease) +
                             "): built for " + container.release_build_id +
                             ", running " + running_id);
      return;
    }

    // THE NEGATIVE CONTROL'S SUPPLY SIDE, and the only reason a non-zero
    // offset reaches the runtime. The runtime gates its assertion path on
    // `pool_offset != 0`, so without a supplied offset that independent
    // instrument is unreachable and a green `tpool_status` would be
    // compile evidence wearing a measurement's clothes.
    //
    // The default is the historical 0xd4a8 because that is the value the
    // control is specified against. It is a DEFAULT, not a special case: it
    // applies to every target rather than one selector, nothing derives an
    // identity from it (the derived `tpool_*` scan is the producer), and an
    // operator overrides it per run by dropping a value beside the active
    // patch, with no rebuild. Whether 0xd4a8 lands in range in a release
    // that does not exist yet is not knowable in advance.
    //
    // DIAGNOSTIC. Remove with the investigation: removing it costs only
    // this block.
    int64_t assert_pool_offset = kDefaultAssertPoolOffset;
    {
      const std::string sidecar = path + ".assert_pool_offset";
      auto sc = fml::FileMapping::CreateReadOnly(sidecar);
      if (sc && sc->GetSize() > 0) {
        const std::string raw(reinterpret_cast<const char*>(sc->GetMapping()),
                              sc->GetSize());
        char* end = nullptr;
        // Base 0: `0x...` is hex, bare digits decimal.
        const long long v = ::strtoll(raw.c_str(), &end, 0);
        // A PARSE FAILURE IS REPORTED, NEVER ABSORBED. The default stands
        // so the control still runs, and `pool_offset=` in the trace
        // records what was ACTUALLY supplied.
        if (end == raw.c_str()) {
          RouteBReport(path,
                       "assert-pool-offset sidecar UNPARSEABLE, keeping "
                       "default 0xd4a8");
        } else {
          assert_pool_offset = static_cast<int64_t>(v);
        }
      }
    }
    {
      std::ostringstream ao;
      ao << "assert-pool-offset supplied=" << std::hex << std::showbase
         << assert_pool_offset;
      RouteBReport(path, ao.str());
    }

    int applied = 0;
    for (const auto& target : container.targets) {
      Dart_RouteBTrace trace;
      // The target's pool location is DERIVED inside
      // Dart_RouteBActivatePatchTraced from the target's own identity, so
      // it needs nothing supplied. What is passed as `pool_offset` is the
      // ASSERTION's subject ("does this slot hold the target?"), answered
      // independently of the derived scan and reported beside it in the
      // same trace. The two instruments are wired to disagree, which is
      // what makes a disagreement evidence.
      const int32_t result = Dart_RouteBActivatePatchTraced(
          target.bytecode, static_cast<intptr_t>(target.length),
          target.library.c_str(), target.selector.c_str(), &trace,
          /*caller_name=*/nullptr, assert_pool_offset);
      // FOR EVERY TARGET, SUCCESS INCLUDED. The success path below `continue`s,
      // so until now a target that "worked" contributed nothing but the
      // applied N/N tally — which is exactly how `applied 1/1` came to be the
      // entire story for four device runs.
      RouteBTraceWrite(path, FormatRouteBTrace(trace, target.library,
                                               target.selector, target.length));
      if (result == Dart_RouteB_Ok) {
        applied++;
        continue;
      }
      // Named, not numbered. A code nobody can read is a bool with extra
      // steps.
      const char* reason = "unknown";
      switch (result) {
        case Dart_RouteB_NoIsolate:
          reason = "no-isolate";
          break;
        case Dart_RouteB_EmptyPayload:
          reason = "empty-payload";
          break;
        case Dart_RouteB_InvalidBytecode:
          reason = "invalid-bytecode";
          break;
        case Dart_RouteB_TargetMissing:
          reason = "target-missing";
          break;
        case Dart_RouteB_AlreadyInterpreted:
          reason = "already-interpreted";
          break;
        case Dart_RouteB_AttachFailed:
          reason = "attach-failed";
          break;
        case Dart_RouteB_Unsupported:
          reason = "unsupported-build";
          break;
        case Dart_RouteB_OutOfMemory:
          reason = "out-of-memory";
          break;
        default:
          break;
      }
      RouteBReport(path, "target " + target.selector + " in " + target.library +
                             " REFUSED (" + reason + ")");
    }
    RouteBReport(path, "applied " + std::to_string(applied) + "/" +
                           std::to_string(container.targets.size()) +
                           " targets, entering main");
  };
}
#endif  // SHOREBIRD_ROUTE_B

bool ConfigureShorebird(const ShorebirdConfigArgs& args,
                        std::string& patch_path) {
  patch_path.clear();
  auto shorebird_updater_dir_name = "shorebird_updater";

  // Parse app id from shorebird.yaml
  std::string app_id = GetValueFromYaml(args.shorebird_yaml, "app_id");
  if (app_id.empty()) {
    FML_LOG(ERROR) << "Shorebird updater: appid not found in shorebird.yaml";
    return false;
  }

#if FML_OS_ANDROID || FML_OS_IOS
  // The app's own sandbox, so nothing else writes here.
  auto code_cache_dir =
      fml::paths::JoinPaths({args.code_cache_path, shorebird_updater_dir_name});
  auto app_storage_dir = fml::paths::JoinPaths(
      {args.app_storage_path, shorebird_updater_dir_name});
#else
  // Desktop directories are the user's, shared by every app they run.
  auto code_cache_dir = fml::paths::JoinPaths(
      {args.code_cache_path, shorebird_updater_dir_name, app_id});
  auto app_storage_dir = fml::paths::JoinPaths(
      {args.app_storage_path, shorebird_updater_dir_name, app_id});
#endif

  fml::CreateDirectory(fml::paths::GetCachesDirectory(),
                       {shorebird_updater_dir_name},
                       fml::FilePermission::kReadWrite);

  // Combine version and version_code into a single string.
  // We could also pass these separately through to the updater if needed.
  auto release_version = args.release_version.version;
  if (!args.release_version.build_number.empty()) {
    release_version += "+" + args.release_version.build_number;
  }

  shorebird::AppConfig config;
  config.release_version = release_version;
  config.original_libapp_paths = args.release_app_library_paths;
  config.app_storage_dir = app_storage_dir;
  config.code_cache_dir = code_cache_dir;
  config.file_callbacks = ShorebirdFileCallbacks();
  config.yaml_config = args.shorebird_yaml;

  // iOS and macOS patches are diffed against the Dart snapshot regions of
  // App.framework/App rather than the whole file, so re-signing the app after
  // release (notarization, the App Store) doesn't invalidate them.
  // FileCallbacksImpl serves those regions to the updater from vm_snapshot,
  // which the updater reads only when it installs a patch, never in Init.
  // Resolved through the VM path, which never returns a patch.
#if FML_OS_MACOSX
  Settings base_settings;
  base_settings.application_library_paths = args.release_app_library_paths;
#if SHOREBIRD_USE_INTERPRETER
  // The interpreter also executes a linked patch out of the base image, so
  // there is no booting without it.
  SetBaseSnapshot(ResolveBaseVMSnapshot(base_settings));
#else
  // Resolved once per process. The base image doesn't change, and the first
  // engine's update thread reads vm_snapshot (FileCallbacksImpl::Open) while a
  // second engine (desktop multi-window) configures, so replacing it here
  // would race that read.
  if (!vm_snapshot) {
    vm_snapshot = DartSnapshot::VMSnapshotFromSettings(base_settings);
  }
  if (!vm_snapshot) {
    FML_LOG(ERROR) << "Shorebird updater: failed to resolve the base snapshot";
  }
#endif  // SHOREBIRD_USE_INTERPRETER
#endif  // FML_OS_MACOSX

  // A second engine in the same process (desktop multi-window) gets
  // AlreadyInitialized here, which the updater documents as safe to ignore:
  // the process is still configured, and the patch path below is the one to
  // boot. Fail only when no Init in this process has succeeded.
  // https://github.com/shorebirdtech/shorebird/issues/3888
  bool init_result = shorebird::Updater::Instance().Init(config) ||
                     shorebird::Updater::IsInitialized();

  FML_LOG(INFO) << "Checking for active patch";
#if SHOREBIRD_ROUTE_B
  // ONE call: validate, select, and record the selection as the booting patch.
  // Splitting these apart is what let a rejected patch stay credited as the
  // booting patch and get the last-known-good one deleted. A second engine
  // gets the first call's path, so every engine in the process boots the same
  // patch. Nothing below, and nothing in snapshot resolution, reports a launch
  // start: the preparation already did.
  std::string active_path =
      shorebird::Updater::Instance().PrepareNextBootPatch();
#else
  shorebird::Updater::Instance().ValidateNextBootPatch();
  std::string active_path = shorebird::Updater::Instance().NextBootPatchPath();
#endif
  if (!active_path.empty()) {
    patch_path = active_path;
    FML_LOG(INFO) << "Shorebird updater: patch path: " << patch_path;
  } else {
    FML_LOG(INFO) << "Shorebird updater: no active patch.";
  }

  // Launch reporting does not happen here. ResolveIsolateData in
  // runtime/dart_snapshot.cc reports the start, so a FlutterEngineGroup or
  // add-to-app host that calls ConfigureShorebird() without ever creating a
  // Shell does not record a boot that never happened. The update thread
  // starts from the Shell's launch-success report, after the boot is
  // recorded; see the Updater class comment.
  return init_result;
}

bool LoadPatchOrBase(const std::string& patch_path,
                     const std::string& base_path,
                     const std::function<bool(const std::string&)>& load) {
  if (patch_path.empty()) {
    return load(base_path);
  }
  if (load(patch_path)) {
    return true;
  }
  FML_LOG(ERROR) << "Shorebird: patch load failed, running base code from "
                 << base_path << " instead of " << patch_path;
  // The failure report marks the booting patch bad, and a patch is only
  // booting once the start is reported. Both are guarded once per process,
  // so the Shell's own start report (ResolveIsolateData) is a no-op, and its
  // success report does not record the base-code boot as the patch's. This
  // is the same sequence TryLoadFromPatch runs with the interpreter, and
  // ApplyPatchToSettings runs on iOS without it.
#if !SHOREBIRD_ROUTE_B
  // A Route B engine's PrepareNextBootPatch already recorded the start.
  shorebird::Updater::Instance().ReportLaunchStart();
#endif
  shorebird::Updater::Instance().ReportLaunchFailure();
  return load(base_path);
}

void ApplyPatchToSettings(const std::string& patch_path, Settings& settings) {
  if (patch_path.empty()) {
    return;
  }
#if SHOREBIRD_ROUTE_B
  // WHAT THIS ARTIFACT IS is decided by its BYTES, before anything touches
  // application_library_paths. The updater installs every code artifact as
  // `patches/{N}/dlc.vmcode` whatever is inside it, so the name says nothing.
  // Handing a Route B container to the VM as the app's library is not a
  // degraded patch, it is a failure to boot: it is bytecode plus a header, not
  // a snapshot, and is applied by the pre-main hook once a root isolate exists.
  //
  // This is also where the hook gets armed on every engine in the process,
  // including a second one. ConfigureShorebird's result is deliberately not
  // consulted: a second call's Init fails by design ("Updater already
  // initialized"), and the arming used to sit behind that early return, so
  // engine two ran the original AOT code while engine one ran the patch. A
  // genuine init failure yields no patch path, and an empty path never gets
  // here.
  if (route_b::SniffFile(patch_path) == route_b::ContainerStatus::kOk) {
    FML_LOG(INFO) << "Shorebird updater: active patch is a Route B container";
    InstallRouteBActivationHook(settings, patch_path);
    return;
  }
#endif  // SHOREBIRD_ROUTE_B
#if SHOREBIRD_USE_INTERPRETER
  // Front-insert rather than clear. A linked patch carries only the code it
  // rewrote, and everything it linked against still executes out of the base
  // image, so the base has to stay resolvable for the life of the process.
  // SearchMapping takes the first path that resolves a symbol, so the patch
  // still wins the lookup without evicting the base.
  settings.application_library_paths.insert(
      settings.application_library_paths.begin(), patch_path);
#elif FML_OS_IOS && SHOREBIRD_ROUTE_B
  // A Route B engine runs Route B containers only. Anything else is a linked
  // .vmcode file, which swapped in for the app library would leave the VM with
  // no snapshot and crash at launch. Keep the release's code and mark the
  // patch failed, as LoadPatchOrBase does, so the updater stops booting it.
  //
  // Deliberately NOT the "cannot run iOS patches" wording of the branch below:
  // the Shorebird CLI reads that text in the binary as this engine declining
  // iOS patches, and this engine does not.
  FML_LOG(ERROR) << "Shorebird: ignoring a patch that is not a Route B "
                    "container, running the release's code instead of "
                 << patch_path;
  shorebird::Updater::Instance().ReportLaunchFailure();
#elif FML_OS_IOS
  // SELFHOST: an iOS engine built without the interpreter (see
  // shorebird_use_interpreter in common/config.gni) cannot run a patch. The
  // patch is a linked .vmcode file, not a Mach-O, so swapping it in for the
  // app library would leave the VM with no snapshot and crash at launch.
  // FlutterDartProject.mm doesn't start the updater in such an engine; this
  // guards any other caller. Keep the release's code and mark the patch failed,
  // as LoadPatchOrBase does, so the updater stops booting it.
  //
  // Load-bearing text: the Shorebird CLI searches the iOS release binary for
  // "engine cannot run iOS patches" (see FlutterDartProject.mm). Keep it.
  FML_LOG(ERROR) << "Shorebird: this engine cannot run iOS patches, running "
                    "the release's code instead of "
                 << patch_path;
  shorebird::Updater::Instance().ReportLaunchStart();
  shorebird::Updater::Instance().ReportLaunchFailure();
#else
  settings.application_library_paths.clear();
  settings.application_library_paths.emplace_back(patch_path);
#endif
}

void* FileCallbacksImpl::Open() {
  // Only iOS and macOS set a base snapshot. Their patches are generated from
  // just the Dart parts of the snapshot, excluding the Mach-O headers and code
  // signature, which change on every build and whenever the app is re-signed.
  if (!vm_snapshot) {
    return nullptr;
  }
  // The VM path is patch-blind, and this stream must be the unpatched base.
  return SnapshotsDataHandle::createForSnapshots(*vm_snapshot).release();
}

uintptr_t FileCallbacksImpl::Read(void* file,
                                  uint8_t* buffer,
                                  uintptr_t length) {
  return reinterpret_cast<SnapshotsDataHandle*>(file)->Read(buffer, length);
}

int64_t FileCallbacksImpl::Seek(void* file, int64_t offset, int32_t whence) {
  // Currently we only support blob handles.
  return reinterpret_cast<SnapshotsDataHandle*>(file)->Seek(offset, whence);
}

void FileCallbacksImpl::Close(void* file) {
  delete reinterpret_cast<SnapshotsDataHandle*>(file);
}

}  // namespace flutter