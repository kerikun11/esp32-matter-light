#include "device_common/matter/matter_service.h"

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_attribute.h>
#include <platform/CHIPDeviceLayer.h>
namespace device_common {
MatterStatus matterStatus() {
  chip::DeviceLayer::StackLock lock;
  auto& server = chip::Server::GetInstance();
  return {server.GetFabricTable().FabricCount() > 0,
          server.GetCommissioningWindowManager().IsCommissioningWindowOpen()};
}
bool openCommissioningWindow(uint16_t timeout_seconds) {
  chip::DeviceLayer::StackLock lock;
  auto& window = chip::Server::GetInstance().GetCommissioningWindowManager();
  if (window.IsCommissioningWindowOpen()) return true;
  const auto err = window.OpenBasicCommissioningWindow(chip::System::Clock::Seconds32(timeout_seconds));
  if (err != CHIP_NO_ERROR) ESP_LOGE("Matter", "Open window: %" CHIP_ERROR_FORMAT, err.Format());
  return err == CHIP_NO_ERROR;
}
bool removeFabric(uint8_t index) {
  chip::DeviceLayer::StackLock lock;
  const auto err = chip::Server::GetInstance().GetFabricTable().Delete(index);
  if (err != CHIP_NO_ERROR) ESP_LOGE("Matter", "Remove fabric: %" CHIP_ERROR_FORMAT, err.Format());
  return err == CHIP_NO_ERROR;
}
void factoryReset() {
  chip::DeviceLayer::StackLock lock;
  chip::DeviceLayer::ConfigurationMgr().InitiateFactoryReset();
}
bool reportOnOff(uint16_t endpoint, bool on) {
  auto value = esp_matter_bool(on);
  const auto err = esp_matter::attribute::report(endpoint, chip::app::Clusters::OnOff::Id,
                                                 chip::app::Clusters::OnOff::Attributes::OnOff::Id, &value);
  if (err != ESP_OK) ESP_LOGE("Matter", "Report OnOff: %s", esp_err_to_name(err));
  return err == ESP_OK;
}
}  // namespace device_common
