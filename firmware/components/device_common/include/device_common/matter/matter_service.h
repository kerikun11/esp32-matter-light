#pragma once
#include <cstdint>
namespace device_common {
struct MatterStatus {
  bool commissioned = false;
  bool commissioning_open = false;
};
MatterStatus matterStatus();
bool openCommissioningWindow(uint16_t timeout_seconds = 300);
bool removeFabric(uint8_t index);
void factoryReset();
bool reportOnOff(uint16_t endpoint, bool on);
}  // namespace device_common
