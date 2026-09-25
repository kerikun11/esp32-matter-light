#pragma once
#include <esp_http_server.h>

#include <string>
namespace device_common {
struct MatterActionResult {
  std::string message;
  bool failed;
};
struct WebAssets {
  const unsigned char* identity;
  size_t identity_size;
  const char* identity_etag;
  const unsigned char* gzip;
  size_t gzip_size;
  const char* gzip_etag;
};
httpd_config_t httpServerConfig();
esp_err_t sendWebPage(httpd_req_t* req, const WebAssets& assets);
esp_err_t sendDeviceInfo(httpd_req_t* req, const char* manual_code, const char* qr_payload);
MatterActionResult handleMatterAction(httpd_req_t* req);
}  // namespace device_common
