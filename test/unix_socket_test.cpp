#include <gtest/gtest.h>

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "config.h"
#include "unix/unix_socket.h"

namespace fs = std::filesystem;

class UnixSocketTest : public ::testing::Test {
 protected:
  void SetUp() override {
    base_dir_ = fs::temp_directory_path() / ("mtdd_uds_test_" + std::to_string(::getpid()));
    fs::create_directories(base_dir_);
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(base_dir_, ec);
  }

  std::string SocketPath(const std::string& name) const { return (base_dir_ / name).string(); }

  mtdd::ServerConfig BaseConfig(const std::string& socket_path) const {
    mtdd::ServerConfig config;
    config.listen_mode = mtdd::ListenMode::Unix;
    config.unix_socket_path = socket_path;
    config.unix_socket_create_dir = true;
    config.unix_socket_dir_mode = 0750;
    config.unix_socket_mode = 0660;
    return config;
  }

  fs::path base_dir_;
};

TEST(UnixSocketPathTest, RejectsRelativePath) {
  EXPECT_THROW(mtdd::unix_socket::ValidateUnixSocketPath("run/mtdd.sock"), std::runtime_error);
}

TEST(UnixSocketPathTest, RejectsTrailingSlash) {
  EXPECT_THROW(mtdd::unix_socket::ValidateUnixSocketPath("/run/mtdd/"), std::runtime_error);
}

TEST(UnixSocketPathTest, RejectsDotDot) {
  EXPECT_THROW(mtdd::unix_socket::ValidateUnixSocketPath("/run/mtdd/../other.sock"), std::runtime_error);
}

TEST(UnixSocketPathTest, RejectsOverlongPath) {
  std::string path = "/run/" + std::string(200, 'a') + ".sock";
  EXPECT_THROW(mtdd::unix_socket::ValidateUnixSocketPath(path), std::runtime_error);
}

TEST_F(UnixSocketTest, PrepareCreatesDirectoryAndRemovesStaleSocket) {
  const std::string nested = (base_dir_ / "nested" / "grpc.sock").string();
  const auto config = BaseConfig(nested);

  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  ASSERT_GE(fd, 0);
  sockaddr_un addr {};
  addr.sun_family = AF_UNIX;
  ::strncpy(addr.sun_path, nested.c_str(), sizeof(addr.sun_path) - 1);
  fs::create_directories(fs::path(nested).parent_path());
  ASSERT_EQ(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
  ::close(fd);

  ASSERT_TRUE(fs::exists(nested));
  EXPECT_NO_THROW(mtdd::unix_socket::PrepareListen(config));
  EXPECT_FALSE(fs::exists(nested));
}

TEST_F(UnixSocketTest, PrepareRejectsLiveSocket) {
  const std::string path = SocketPath("live.sock");
  const auto config = BaseConfig(path);

  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  ASSERT_GE(fd, 0);
  sockaddr_un addr {};
  addr.sun_family = AF_UNIX;
  ::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
  fs::create_directories(base_dir_);
  ASSERT_EQ(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
  ASSERT_EQ(::listen(fd, 1), 0);

  EXPECT_THROW(mtdd::unix_socket::PrepareListen(config), std::runtime_error);
  ::close(fd);
  ::unlink(path.c_str());
}

TEST_F(UnixSocketTest, PrepareRejectsRegularFile) {
  const std::string path = SocketPath("blocked.sock");
  const auto config = BaseConfig(path);
  {
    std::ofstream out(path);
    out << "not a socket";
  }

  EXPECT_THROW(mtdd::unix_socket::PrepareListen(config), std::runtime_error);
}

TEST_F(UnixSocketTest, FinalizeAppliesMode) {
  const std::string path = SocketPath("ready.sock");
  const auto config = BaseConfig(path);
  mtdd::unix_socket::PrepareListen(config);

  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  ASSERT_GE(fd, 0);
  sockaddr_un addr {};
  addr.sun_family = AF_UNIX;
  ::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
  ASSERT_EQ(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
  ::close(fd);

  EXPECT_NO_THROW(mtdd::unix_socket::FinalizeListen(config));

  struct stat st {};
  ASSERT_EQ(::stat(path.c_str(), &st), 0);
  EXPECT_TRUE(S_ISSOCK(st.st_mode));
  EXPECT_EQ(st.st_mode & 0777, 0660);
  mtdd::unix_socket::CleanupListen(path);
  EXPECT_FALSE(fs::exists(path));
}

#endif  // !_WIN32
