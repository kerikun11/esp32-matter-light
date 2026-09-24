/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once

#include <cctype>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

// Reads commands from stdin (the USB-Serial-JTAG console), which the
// console driver puts in non-blocking mode by default.
class CommandParser {
 public:
  void update() {
    int ic;
    while ((ic = getchar()) != EOF) {
      char c = (char)ic;
      switch (c) {
        case '\n':
        case '\r':
          if (!line_.empty()) {
            queue_.push_back(split(line_));
            line_.clear();
          }
          break;
        case '\b':
        case 0x7f:  // DEL, sent by most terminals for backspace
          if (!line_.empty()) line_.pop_back();
          fputs("\b \b", stdout);  // erase last character
          continue;
        case '0' ... '9':
        case 'a' ... 'z':
        case 'A' ... 'Z':
        case '_':
        case '-':
        case ' ':
          line_.push_back(c);
          break;
        default:
          continue;
      }
      putchar(c);
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
