#pragma once
#include <string>

namespace ea::net {

struct TlsConfig {
    std::string ca_cert_path;
    std::string client_cert_path;
    std::string client_key_path;
    bool verify_server = true;

    static std::string detect_ca_path();
};

}  // namespace ea::net
