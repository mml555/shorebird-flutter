// Copyright (c) 2026, the Shorebird self-host fork.
//
// Route B patch containers ("SBRBPTCH"), read on the embedder side.
//
// WHY THIS LIVES HERE AND NOT IN THE VM. The Dart runtime should receive
// already-validated (library, selector, bytes) triples and nothing else. It has
// no business learning JSON, container layout or release identity: those are
// delivery concerns, they change on a different schedule to the VM, and every
// one of them is an embedder-side failure the VM cannot report usefully.
//
// WHY IT IS NOT KEYED ON THE FILENAME. The updater installs every code artifact
// as `{state_root}/patches/{N}/dlc.vmcode`, whatever is inside it. So the name
// says nothing, and `PatchCarriesCode()` -- which keys on `.vmcode` -- is true
// for a Route B container too. Handing one of those to the VM as the app's only
// library path is not a degraded patch, it is a failure to boot. Content
// sniffing is a boot-safety invariant here, not tidiness.

#ifndef FLUTTER_SHELL_COMMON_SHOREBIRD_ROUTE_B_PATCH_H_
#define FLUTTER_SHELL_COMMON_SHOREBIRD_ROUTE_B_PATCH_H_

#include <cstdint>
#include <string>
#include <vector>

namespace flutter {
namespace route_b {

/// Why a container was refused, or kOk.
///
/// One code per cause. The whole point is that a stale payload says
/// kWrongRelease and never degrades into an opaque attach failure -- Route B
/// has lost enough hours to exactly that collapse.
enum class ContainerStatus {
  kOk,
  /// Magic did not match. The overwhelmingly common case: an ordinary code
  /// patch. Not an error, and callers must treat it as "not mine".
  kNotAContainer,
  /// Magic matched, formatVersion did not. Refuse rather than guess: a reader
  /// that tolerates unknown versions defines the format by accident.
  kUnsupportedVersion,
  /// Truncated, unparseable, missing required fields, or offsets that run past
  /// the end of the file.
  kMalformed,
  /// Built for a different release. Checked BEFORE any target is resolved.
  kWrongRelease,
  /// A payload's bytes do not match its declared sha256.
  kPayloadCorrupt,
};

const char* StatusName(ContainerStatus status);

/// One function to replace. `bytecode` points into the caller's mapping and is
/// valid only as long as that mapping is.
struct Target {
  std::string library;
  std::string selector;
  const uint8_t* bytecode = nullptr;
  size_t length = 0;
};

struct Container {
  std::string release_build_id;
  std::vector<Target> targets;
};

/// Cheap first-8-bytes check, for deciding whether a path is a Route B
/// container at all. Returns kOk or kNotAContainer; never reads the header.
///
/// Separate from ParseContainer because the decision it feeds -- whether to put
/// this path into application_library_paths -- happens in ConfigureShorebird,
/// long before an isolate exists to validate a release against.
ContainerStatus SniffFile(const std::string& path);

/// Full structural parse and integrity check.
///
/// Does NOT check release identity: that needs a live isolate, so the caller
/// compares `release_build_id` itself. Everything this CAN check, it checks
/// before returning, so a caller that gets kOk holds a container whose payloads
/// are intact.
ContainerStatus Parse(const uint8_t* bytes,
                      size_t length,
                      Container* out,
                      std::string* error);

}  // namespace route_b
}  // namespace flutter

#endif  // FLUTTER_SHELL_COMMON_SHOREBIRD_ROUTE_B_PATCH_H_
