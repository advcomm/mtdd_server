#include "unix/unix_socket.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#include "logging.h"

namespace mtdd::unix_socket {
namespace {

constexpr std::size_t kMaxUnixPathLength = 107;

std::string ErrnoMessage(const char* context) {
  return std::string(context) + ": " + std::strerror(errno);
}

std::string ParentDirectory(const std::string& path) {
  const auto pos = path.rfind('/');
  if (pos == std::string::npos || pos == 0) {
    return "/";
  }
  return path.substr(0, pos);
}

bool PathContainsDotDot(const std::string& path) {
  if (path == "..") {
    return true;
  }
  if (path.rfind("../", 0) == 0) {
    return true;
  }
  return path.find("/../") != std::string::npos || path.size() >= 3 && path.substr(path.size() - 3) == "/..";
}

}  // namespace

void ValidateUnixSocketPath(const std::string& path) {
  if (path.empty()) {
    throw std::runtime_error("unix socket path must not be empty");
  }
  if (path[0] != '/') {
    throw std::runtime_error("unix socket path must be absolute");
  }
  if (path.size() > kMaxUnixPathLength) {
    throw std::runtime_error(
        "unix socket path exceeds " + std::to_string(kMaxUnixPathLength) +
        " bytes (Linux sockaddr_un limit): " + path);
  }
  if (path.back() == '/') {
    throw std::runtime_error("unix socket path must not end with '/': " + path);
  }
  if (path == "/") {
    throw std::runtime_error("unix socket path must name a socket file, not the filesystem root");
  }
  if (PathContainsDotDot(path)) {
    throw std::runtime_error("unix socket path must not contain '..' components: " + path);
  }
}

#ifndef _WIN32
namespace {

void EnsureDirectory(const std::string& dir, mode_t mode, bool create) {
  struct stat st {};
  if (::stat(dir.c_str(), &st) == 0) {
    if (!S_ISDIR(st.st_mode)) {
      throw std::runtime_error("unix socket parent path is not a directory: " + dir);
    }
    if (::access(dir.c_str(), W_OK | X_OK) != 0) {
      throw std::runtime_error(
          "unix socket directory is not writable: " + dir + " (" + std::strerror(errno) +
          "); ensure systemd RuntimeDirectory=mtdd or add the service user to the directory group");
    }
    return;
  }

  if (errno != ENOENT) {
    throw std::runtime_error(ErrnoMessage(("stat unix socket directory " + dir).c_str()));
  }
  if (!create) {
    throw std::runtime_error(
        "unix socket directory does not exist: " + dir +
        " (set MTDD_UNIX_SOCKET_CREATE_DIR=1 or configure systemd RuntimeDirectory=mtdd)");
  }

  const std::string parent = ParentDirectory(dir);
  if (parent != dir) {
    EnsureDirectory(parent, mode, true);
  }

  if (::mkdir(dir.c_str(), mode) != 0 && errno != EEXIST) {
    throw std::runtime_error(ErrnoMessage(("mkdir unix socket directory " + dir).c_str()));
  }

  if (::stat(dir.c_str(), &st) != 0) {
    throw std::runtime_error(ErrnoMessage(("stat unix socket directory " + dir).c_str()));
  }
  if (!S_ISDIR(st.st_mode)) {
    throw std::runtime_error("unix socket parent path is not a directory: " + dir);
  }
  if (::access(dir.c_str(), W_OK | X_OK) != 0) {
    throw std::runtime_error(
        "created unix socket directory is not writable: " + dir + " (" + std::strerror(errno) + ")");
  }
}

bool IsLiveUnixSocket(const std::string& path) {
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    throw std::runtime_error(ErrnoMessage("socket(AF_UNIX) probe failed"));
  }

  sockaddr_un addr {};
  addr.sun_family = AF_UNIX;
  if (path.size() >= sizeof(addr.sun_path)) {
    ::close(fd);
    throw std::runtime_error("unix socket path too long for sockaddr_un probe: " + path);
  }
  std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

  const bool connected = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
  ::close(fd);
  return connected;
}

void RemoveStaleSocketFile(const std::string& path) {
  struct stat st {};
  if (::stat(path.c_str(), &st) != 0) {
    if (errno == ENOENT) {
      return;
    }
    throw std::runtime_error(ErrnoMessage(("stat unix socket " + path).c_str()));
  }

  if (S_ISDIR(st.st_mode)) {
    throw std::runtime_error("unix socket path is a directory: " + path);
  }
  if (!S_ISSOCK(st.st_mode)) {
    throw std::runtime_error(
        "unix socket path is occupied by a non-socket file: " + path +
        " (remove it or choose a different MTDD_LISTEN path)");
  }

  if (IsLiveUnixSocket(path)) {
    throw std::runtime_error(
        "another process is already listening on unix socket: " + path +
        " (stop the other mtdd_server instance or choose a different MTDD_LISTEN path)");
  }

  if (::unlink(path.c_str()) != 0) {
    throw std::runtime_error(ErrnoMessage(("unlink stale unix socket " + path).c_str()));
  }
  log::Info("unix_socket_stale_removed", path);
}

void VerifySocketNode(const std::string& path, mode_t expected_mode) {
  struct stat st {};
  if (::stat(path.c_str(), &st) != 0) {
    throw std::runtime_error(ErrnoMessage(("stat bound unix socket " + path).c_str()));
  }
  if (!S_ISSOCK(st.st_mode)) {
    throw std::runtime_error("expected unix socket after bind, found non-socket at: " + path);
  }

  const mode_t actual_mode = st.st_mode & 0777;
  if (actual_mode != expected_mode) {
    log::Warn(
        "unix_socket_mode_mismatch",
        path + " expected=" + std::to_string(expected_mode) + " actual=" + std::to_string(actual_mode));
  }
}

}  // namespace

void PrepareListen(const ServerConfig& config) {
  ValidateUnixSocketPath(config.unix_socket_path);

  const std::string dir = ParentDirectory(config.unix_socket_path);
  EnsureDirectory(dir, static_cast<mode_t>(config.unix_socket_dir_mode), config.unix_socket_create_dir);
  RemoveStaleSocketFile(config.unix_socket_path);
}

void FinalizeListen(const ServerConfig& config) {
  const mode_t mode = static_cast<mode_t>(config.unix_socket_mode);
  if (::chmod(config.unix_socket_path.c_str(), mode) != 0) {
    throw std::runtime_error(ErrnoMessage(("chmod unix socket " + config.unix_socket_path).c_str()));
  }
  VerifySocketNode(config.unix_socket_path, mode);
  log::Info("unix_socket_ready", config.unix_socket_path + " mode=" + std::to_string(mode));
}

void CleanupListen(const std::string& path) {
  struct stat st {};
  if (::stat(path.c_str(), &st) != 0) {
    if (errno == ENOENT) {
      return;
    }
    log::Warn("unix_socket_cleanup_stat_failed", path + " (" + std::strerror(errno) + ")");
    return;
  }
  if (!S_ISSOCK(st.st_mode)) {
    log::Warn("unix_socket_cleanup_skipped", path + " (not a socket)");
    return;
  }
  if (::unlink(path.c_str()) != 0) {
    log::Warn("unix_socket_cleanup_failed", path + " (" + std::strerror(errno) + ")");
    return;
  }
  log::Info("unix_socket_removed", path);
}

ScopedListen::ScopedListen(ServerConfig config) : config_(std::move(config)) {}

ScopedListen::~ScopedListen() {
  if (finalized_) {
    CleanupListen(config_.unix_socket_path);
  }
}

void ScopedListen::Prepare() {
  PrepareListen(config_);
  prepared_ = true;
}

void ScopedListen::Finalize() {
  if (!prepared_) {
    throw std::runtime_error("ScopedListen::Finalize called before Prepare");
  }
  FinalizeListen(config_);
  finalized_ = true;
}

void ScopedListen::Abort() {
  if (prepared_ && !finalized_) {
    CleanupListen(config_.unix_socket_path);
    prepared_ = false;
  }
}

#endif  // !_WIN32

}  // namespace mtdd::unix_socket
