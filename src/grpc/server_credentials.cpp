#include "grpc/server_credentials.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace mtdd::grpc_util {
namespace {

std::string ReadPemFile(const char* env_name, const std::string& path) {
  if (path.empty()) {
    throw std::runtime_error(std::string(env_name) + " is required when MTDD_GRPC_TLS is enabled");
  }

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("failed to read " + std::string(env_name) + " at " + path);
  }

  std::ostringstream contents;
  contents << input.rdbuf();
  const std::string pem = contents.str();
  if (pem.empty()) {
    throw std::runtime_error(std::string(env_name) + " is empty: " + path);
  }
  return pem;
}

}  // namespace

std::shared_ptr<::grpc::ServerCredentials> BuildServerCredentials(const GrpcTlsConfig& tls) {
  if (!tls.enabled) {
    return ::grpc::InsecureServerCredentials();
  }

  ::grpc::SslServerCredentialsOptions options;
  options.pem_key_cert_pairs.push_back(
      {ReadPemFile("MTDD_GRPC_TLS_KEY_FILE", tls.key_file),
       ReadPemFile("MTDD_GRPC_TLS_CERT_FILE", tls.cert_file)});

  if (!tls.client_ca_file.empty()) {
    options.pem_root_certs = ReadPemFile("MTDD_GRPC_TLS_CLIENT_CA_FILE", tls.client_ca_file);
    options.client_certificate_request =
        GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY;
  }

  return ::grpc::SslServerCredentials(options);
}

}  // namespace mtdd::grpc_util
