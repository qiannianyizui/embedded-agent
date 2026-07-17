// src/mcp/StdioTransport.h
#pragma once
#include "ITransport.h"
#include <map>
#include <string>
#include <vector>
#include <memory>

namespace ea::mcp {

class StdioTransport : public ITransport {
public:
    struct Config {
        std::string command;
        std::vector<std::string> args;
        std::map<std::string, std::string> env;
    };

    explicit StdioTransport(Config config);
    ~StdioTransport() override;

    Result<void> start() override;
    Result<void> stop() override;
    Result<nlohmann::json> send(const nlohmann::json& request) override;
    bool is_running() const override;

private:
    Config config_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ea::mcp
