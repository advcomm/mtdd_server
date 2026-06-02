#pragma once

#include <string>

#include "config.h"

namespace mtdd::unix_socket {

// Static path rules (no filesystem access).
void ValidateUnixSocketPath(const std::string& path);

#ifndef _WIN32

// Ensures the parent directory exists and removes stale socket files before bind.
void PrepareListen(const ServerConfig& config);

// Verifies the socket was created and applies MTDD_UNIX_SOCKET_MODE.
void FinalizeListen(const ServerConfig& config);

// Removes the bound socket file on shutdown.
void CleanupListen(const std::string& path);

class ScopedListen {
 public:
  explicit ScopedListen(ServerConfig config);
  ~ScopedListen();

  ScopedListen(const ScopedListen&) = delete;
  ScopedListen& operator=(const ScopedListen&) = delete;

  void Prepare();
  void Finalize();
  void Abort();

 private:
  ServerConfig config_;
  bool prepared_ = false;
  bool finalized_ = false;
};

#endif  // !_WIN32

}  // namespace mtdd::unix_socket
