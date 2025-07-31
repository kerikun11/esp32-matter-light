/**
 * @copyright 2025 Ryotaro Onuki
 * SPDX-License-Identifier: LGPL-2.1
 */
#pragma once

#include <Arduino.h>

#include <cctype>
#include <deque>
#include <string>
#include <vector>

class CommandParser {
 public:
  explicit CommandParser(Stream& io, size_t maxLineLen = 128)
      : io_(io), maxLen_(maxLineLen) {}

  void update() {
    while (io_.available()) {
      char c = (char)io_.read();
      io_.print(c);
      if (c == '\r' || c == '\n') {
        if (!line_.empty()) {
          queue_.push_back(split(line_));
          line_.clear();
        }
      } else {
        if (line_.size() + 1 < maxLen_)
          line_.push_back(c);
        else {
          line_.clear();
          io_.println(F("ERR: line too long"));
        }
      }
    }
  }

  int available() const { return queue_.size(); }

  std::vector<std::string> get() {
    if (queue_.empty()) return {};
    auto cmd = std::move(queue_.front());
    queue_.pop_front();
    return cmd;
  }

 private:
  Stream& io_;
  size_t maxLen_;
  std::string line_;
  std::deque<std::vector<std::string> > queue_;

  static std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0, n = s.size();
    while (i < n) {
      while (i < n && std::isspace((unsigned char)s[i])) ++i;  // 前空白
      if (i >= n) break;
      size_t j = i;
      while (j < n && !std::isspace((unsigned char)s[j])) ++j;  // 単語末
      out.emplace_back(s.substr(i, j - i));
      i = j;
    }
    return out;
  }
};
