#pragma once
#include "core/ITool.h"

namespace ea::net { class HttpClient; }

namespace ea::tool {

class WebTool : public ITool {
public:
    explicit WebTool(net::HttpClient* client = nullptr) : client_(client) {}

    std::string name() const override { return "web"; }
    std::string description() const override { return "Web search and fetch operations"; }
    json parameters_schema() const override;
    Result<ToolResult> execute(const json& args) override;

private:
    net::HttpClient* client_;
};

}  // namespace ea::tool
