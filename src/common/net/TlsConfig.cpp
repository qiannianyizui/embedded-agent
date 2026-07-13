#include "TlsConfig.h"
#include "common/io/FileSystem.h"

namespace ea::net {

std::string TlsConfig::detect_ca_path() {
    static const char* paths[] = {
        "/etc/ssl/certs/ca-certificates.crt",
        "/etc/pki/tls/certs/ca-bundle.crt",
        "/etc/ssl/ca-bundle.pem",
        "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
        "/data/data/com.termux/files/usr/etc/tls/cert.pem",
        nullptr
    };

    for (int i = 0; paths[i] != nullptr; ++i) {
        auto ex = ea::fs::exists(paths[i]);
        if (ex.ok() && ex.value()) {
            return paths[i];
        }
    }
    return "";
}

}  // namespace ea::net
