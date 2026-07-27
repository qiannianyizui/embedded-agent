// src/mcp/ITransport.h
#pragma once
#include "base/Result.h"
#include "nlohmann/json.hpp"

namespace ea::mcp {

class ITransport {
public:
    virtual ~ITransport() = default;

    virtual Result<void> start() = 0;
    virtual Result<void> stop() = 0;

    // Send a JSON-RPC request and wait for the response
    virtual Result<nlohmann::json> send(const nlohmann::json& request) = 0;

    virtual bool is_running() const = 0;
};

}  // namespace ea::mcp
