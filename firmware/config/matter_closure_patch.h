#pragma once

// clang-format off

// GCC 14 + C++20 requires same-type operator== for std::optional<T> comparison.
// ClosureControl structs only define operator==(const BaseType&), which is not
// sufficient. This header adds free-function operators via ADL without modifying
// managed_components.
//
// Injected into espressif__esp_matter via CMake -include (see firmware/CMakeLists.txt).
// Vendored from arduino-esp32's libraries/Matter/src/matter_closure_patch.h now that
// this project no longer depends on arduino-esp32.
// See: https://github.com/espressif/arduino-esp32/issues/12851

#ifdef __cplusplus

#include "app/clusters/closure-control-server/closure-control-cluster-objects.h"

namespace chip {
namespace app {
namespace Clusters {
namespace ClosureControl {

inline bool operator==(const GenericOverallCurrentState &a, const GenericOverallCurrentState &b) {
  return a.position == b.position && a.latch == b.latch && a.speed == b.speed && a.secureState == b.secureState;
}

inline bool operator==(const GenericOverallTargetState &a, const GenericOverallTargetState &b) {
  return a.position == b.position && a.latch == b.latch && a.speed == b.speed;
}

}  // namespace ClosureControl
}  // namespace Clusters
}  // namespace app
}  // namespace chip

#endif  // __cplusplus
