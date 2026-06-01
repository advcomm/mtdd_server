#pragma once

#include <chrono>
#include <iostream>
#include <mutex>
#include <string>

namespace mtdd::log {

inline std::mutex& Mutex() {
  static std::mutex m;
  return m;
}

inline void Info(const std::string& event, const std::string& detail = {}) {
  std::lock_guard<std::mutex> lock(Mutex());
  std::cerr << "{\"level\":\"info\",\"event\":\"" << event << "\"";
  if (!detail.empty()) {
    std::cerr << ",\"detail\":\"" << detail << "\"";
  }
  std::cerr << "}\n";
}

inline void Warn(const std::string& event, const std::string& detail = {}) {
  std::lock_guard<std::mutex> lock(Mutex());
  std::cerr << "{\"level\":\"warn\",\"event\":\"" << event << "\"";
  if (!detail.empty()) {
    std::cerr << ",\"detail\":\"" << detail << "\"";
  }
  std::cerr << "}\n";
}

inline void Error(const std::string& event, const std::string& detail = {}) {
  std::lock_guard<std::mutex> lock(Mutex());
  std::cerr << "{\"level\":\"error\",\"event\":\"" << event << "\"";
  if (!detail.empty()) {
    std::cerr << ",\"detail\":\"" << detail << "\"";
  }
  std::cerr << "}\n";
}

class ScopedTimer {
 public:
  explicit ScopedTimer(std::string event) : event_(std::move(event)), start_(std::chrono::steady_clock::now()) {}

  ~ScopedTimer() {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start_)
                        .count();
    std::lock_guard<std::mutex> lock(Mutex());
    std::cerr << "{\"level\":\"info\",\"event\":\"" << event_ << "\",\"duration_ms\":" << ms << "}\n";
  }

 private:
  std::string event_;
  std::chrono::steady_clock::time_point start_;
};

}  // namespace mtdd::log
