// src/server/ServerConfig.h
#pragma once
#include <string>
#include <chrono>

namespace ea::server {

struct ServerConfig {
    std::string host = "0.0.0.0";
    int port = 8080;
    int max_sessions = 100;
    std::string cors_origin = "*";
    std::chrono::seconds session_idle_timeout{3600};
};

}  // namespace ea::server
