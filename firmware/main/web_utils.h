/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once

#include <WebServer.h>

#include "app_log.h"

inline void replaceTemplateValue(String& html, const char* key,
                                 const String& value) {
  html.replace(key, value);
}

inline String escapeHtml(const char* value) {
  String escaped(value);
  escaped.replace("&", "&amp;");
  escaped.replace("\"", "&quot;");
  escaped.replace("<", "&lt;");
  escaped.replace(">", "&gt;");
  escaped.replace("'", "&#39;");
  return escaped;
}

inline void logRequest(WebServer& server) {
  const char* method = server.method() == HTTP_GET ? "GET" : "POST";
  if (server.args() == 0) {
    LOGI("[Web] %s %s", method, server.uri().c_str());
    return;
  }
  String args;
  for (int i = 0; i < server.args(); i++) {
    if (i > 0) args += ' ';
    args += server.argName(i);
    args += '=';
    args += server.arg(i);
  }
  LOGI("[Web] %s %s %s", method, server.uri().c_str(), args.c_str());
}

inline void redirectRoot(WebServer& server) {
  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "");
}
