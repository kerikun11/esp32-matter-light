/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once

#include <esp_http_server.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "app_log.h"

inline void replaceTemplateValue(std::string& html, const char* key,
                                 const std::string& value) {
  const size_t key_len = strlen(key);
  size_t pos = 0;
  while ((pos = html.find(key, pos)) != std::string::npos) {
    html.replace(pos, key_len, value);
    pos += value.length();
  }
}

inline std::string escapeHtml(const char* value) {
  std::string escaped(value);
  auto replaceAll = [&escaped](const char* from, const char* to) {
    const size_t from_len = strlen(from);
    const size_t to_len = strlen(to);
    size_t pos = 0;
    while ((pos = escaped.find(from, pos)) != std::string::npos) {
      escaped.replace(pos, from_len, to);
      pos += to_len;
    }
  };
  replaceAll("&", "&amp;");
  replaceAll("\"", "&quot;");
  replaceAll("<", "&lt;");
  replaceAll(">", "&gt;");
  replaceAll("'", "&#39;");
  return escaped;
}

inline std::string trim(const std::string& s) {
  const size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  const size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

inline void logRequest(httpd_req_t* req) {
  LOGI("[Web] %s %s", req->method == HTTP_GET ? "GET" : "POST", req->uri);
}

inline void redirectRoot(httpd_req_t* req) {
  httpd_resp_set_status(req, "303 See Other");
  httpd_resp_set_hdr(req, "Location", "/");
  httpd_resp_send(req, nullptr, 0);
}

inline std::string urlDecode(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '+') {
      out += ' ';
    } else if (in[i] == '%' && i + 2 < in.size()) {
      unsigned int value = 0;
      sscanf(in.c_str() + i + 1, "%2x", &value);
      out += static_cast<char>(value);
      i += 2;
    } else {
      out += in[i];
    }
  }
  return out;
}

// Reads and URL-decodes an application/x-www-form-urlencoded POST body.
// Rejects bodies larger than kMaxFormBodyLen (form fields here are all
// short, so this is a generous cap against accidental huge uploads).
inline std::map<std::string, std::string> parseFormBody(httpd_req_t* req) {
  constexpr size_t kMaxFormBodyLen = 2048;
  std::map<std::string, std::string> fields;
  if (req->content_len == 0 || req->content_len > kMaxFormBodyLen) {
    return fields;
  }

  std::string body(req->content_len, '\0');
  size_t received = 0;
  int consecutive_timeouts = 0;
  constexpr int kMaxConsecutiveTimeouts = 5;  // ~25s of dead silence total
  while (received < req->content_len) {
    const int ret = httpd_req_recv(req, body.data() + received,
                                   req->content_len - received);
    if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
      // Recoverable: the client is just slow to send (this device's Wi-Fi
      // link can stall for several seconds under load -- see the /update
      // OTA handler's identical retry loop), not a reason to silently drop
      // the whole form submission.
      if (++consecutive_timeouts > kMaxConsecutiveTimeouts) return {};
      continue;
    }
    if (ret <= 0) return {};
    consecutive_timeouts = 0;
    received += ret;
  }

  size_t pos = 0;
  while (pos < body.size()) {
    size_t amp = body.find('&', pos);
    if (amp == std::string::npos) amp = body.size();
    const size_t eq = body.find('=', pos);
    if (eq != std::string::npos && eq < amp) {
      fields[urlDecode(body.substr(pos, eq - pos))] =
          urlDecode(body.substr(eq + 1, amp - eq - 1));
    } else if (amp > pos) {
      fields[urlDecode(body.substr(pos, amp - pos))] = "";
    }
    pos = amp + 1;
  }
  return fields;
}

inline std::string formValue(const std::map<std::string, std::string>& fields,
                             const char* key) {
  const auto it = fields.find(key);
  return it != fields.end() ? it->second : std::string();
}
