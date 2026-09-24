#pragma once

#include <cstdlib>
#include <string>
#include <string_view>

namespace web_asset {

inline std::string_view trim(std::string_view value) {
  const auto first = value.find_first_not_of(" \t");
  if (first == value.npos) return {};
  return value.substr(first, value.find_last_not_of(" \t") - first + 1);
}

// Explicit codings override '*', including gzip;q=0. Identity is allowed
// by default unless it is explicitly excluded (or excluded by '*;q=0').
inline float quality(std::string_view header, std::string_view coding) {
  float wildcard = -1;
  while (!header.empty()) {
    const auto comma = header.find(',');
    auto item = trim(header.substr(0, comma));
    header = comma == header.npos ? std::string_view{} : header.substr(comma + 1);
    const auto semicolon = item.find(';');
    std::string name(trim(item.substr(0, semicolon)));
    for (char& ch : name) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    float q = 1;
    if (semicolon != item.npos) {
      const auto parameter = trim(item.substr(semicolon + 1));
      q = 0;
      if (parameter.substr(0, 2) == "q=") {
        const std::string value(trim(parameter.substr(2)));
        char* end = nullptr;
        const float parsed = std::strtof(value.c_str(), &end);
        if (end != value.c_str() && *end == '\0' && parsed >= 0 && parsed <= 1) q = parsed;
      }
    }
    if (name == coding) return q;
    if (name == "*") wildcard = q;
  }
  if (coding == "identity") return wildcard == 0 ? 0 : 1;
  return wildcard >= 0 ? wildcard : 0;
}

inline bool etagMatches(std::string_view header, std::string_view etag) {
  while (!header.empty()) {
    const auto comma = header.find(',');
    auto item = trim(header.substr(0, comma));
    header = comma == header.npos ? std::string_view{} : header.substr(comma + 1);
    if (item.substr(0, 2) == "W/") item.remove_prefix(2);
    if (item == "*" || item == etag) return true;
  }
  return false;
}

}  // namespace web_asset
