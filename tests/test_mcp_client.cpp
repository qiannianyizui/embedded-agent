// tests/test_mcp_client.cpp
#include <catch2/catch_test_macros.hpp>
#include "mcp/ITransport.h"
#include "mcp/McpClient.h"
#include "mcp/McpToolAdapter.h"

using namespace ea::mcp;
using ea::Result;

// Helper to wrap a result value in a JSON-RPC response
static nlohmann::json rpc_result(const nlohmann::json& result_value) {
    return {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"result", result_value}
    };
}

static nlohmann::json init_result() {
    return rpc_result({
        {"protocolVersion", "2024-11-05"},
        {"capabilities", nlohmann::json::object()},
        {"serverInfo", {{"name", "test-server"}, {"version", "1.0"}}}
    });
}

class MockTransport : public ITransport {
public:
    std::vector<nlohmann::json> responses;
    std::vector<nlohmann::json> sent_requests;
    bool fail_start = false;
    size_t call_index = 0;

    ea::Result<void> start() override {
        if (fail_start) return ea::Error::io("start failed");
        running_ = true; return {};
    }
    ea::Result<void> stop() override { running_ = false; return {}; }
    ea::Result<nlohmann::json> send(const nlohmann::json& request) override {
        sent_requests.push_back(request);
        if (call_index < responses.size()) {
            return responses[call_index++];
        }
        return ea::Error::io("no more mock responses");
    }
    bool is_running() const override { return running_; }

private:
    bool running_ = false;
};

// Helper to create a connected client with mock transport
static std::shared_ptr<McpClient> make_connected_client(MockTransport* transport) {
    transport->responses.push_back(init_result());
    transport->responses.push_back({{"jsonrpc", "2.0"}, {"id", 2}, {"result", nullptr}});
    auto client = std::make_shared<McpClient>(std::unique_ptr<ITransport>(transport));
    client->connect();
    return client;
}

TEST_CASE("McpClient connect sends initialize request", "[mcp]") {
    auto transport = new MockTransport();
    transport->responses.push_back(init_result());
    transport->responses.push_back({{"jsonrpc", "2.0"}, {"id", 2}, {"result", nullptr}});

    auto client = std::make_shared<McpClient>(std::unique_ptr<ITransport>(transport));
    auto result = client->connect();

    REQUIRE(result.ok());
    REQUIRE(client->is_connected());
    REQUIRE(transport->sent_requests.size() >= 1);
    REQUIRE(transport->sent_requests[0]["method"] == "initialize");
    REQUIRE(transport->sent_requests[0]["params"]["protocolVersion"] == "2024-11-05");
}

TEST_CASE("McpClient connect fails on transport error", "[mcp]") {
    auto transport = new MockTransport();
    transport->fail_start = true;

    auto client = std::make_shared<McpClient>(std::unique_ptr<ITransport>(transport));
    auto result = client->connect();
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(client->is_connected());
}

TEST_CASE("McpClient list_tools returns ToolSpec vector", "[mcp]") {
    auto transport = new MockTransport();
    auto client = make_connected_client(transport);

    transport->responses.push_back(rpc_result({
        {"tools", nlohmann::json::array({
            {{"name", "read_file"}, {"description", "Read a file"}, {"inputSchema", nlohmann::json::object()}},
            {{"name", "write_file"}, {"description", "Write a file"}, {"inputSchema", nlohmann::json::object()}}
        })}
    }));

    auto result = client->list_tools();
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 2);
    REQUIRE(result.value()[0].name == "read_file");
    REQUIRE(result.value()[1].name == "write_file");
}

TEST_CASE("McpClient call_tool sends tools/call and maps response", "[mcp]") {
    auto transport = new MockTransport();
    auto client = make_connected_client(transport);

    transport->responses.push_back(rpc_result({
        {"content", nlohmann::json::array({
            {{"type", "text"}, {"text", "file content here"}}
        })},
        {"isError", false}
    }));

    auto result = client->call_tool("read_file", nlohmann::json{{"path", "/tmp/test"}});
    REQUIRE(result.ok());
    REQUIRE(result.value().output == "file content here");
    REQUIRE(result.value().is_error == false);
}

TEST_CASE("McpClient call_tool maps isError to ToolResult", "[mcp]") {
    auto transport = new MockTransport();
    auto client = make_connected_client(transport);

    transport->responses.push_back(rpc_result({
        {"content", nlohmann::json::array({
            {{"type", "text"}, {"text", "Permission denied"}}
        })},
        {"isError", true}
    }));

    auto result = client->call_tool("read_file", nlohmann::json::object());
    REQUIRE(result.ok());
    REQUIRE(result.value().is_error == true);
    REQUIRE(result.value().output == "Permission denied");
}

TEST_CASE("McpToolAdapter delegates execute to McpClient", "[mcp]") {
    auto transport = new MockTransport();
    auto client = make_connected_client(transport);

    ea::ToolSpec spec{"test_tool", "A test tool", nlohmann::json::object()};
    McpToolAdapter adapter(client, spec, true);

    REQUIRE(adapter.name() == "test_tool");
    REQUIRE(adapter.description() == "A test tool");
    REQUIRE(adapter.is_dangerous() == true);
    REQUIRE(adapter.is_mutating() == true);

    transport->responses.push_back(rpc_result({
        {"content", nlohmann::json::array({
            {{"type", "text"}, {"text", "result"}}
        })},
        {"isError", false}
    }));

    auto result = adapter.execute(nlohmann::json::object());
    REQUIRE(result.ok());
    REQUIRE(result.value().output == "result");
}

TEST_CASE("McpToolAdapter is_dangerous defaults to false", "[mcp]") {
    auto transport = new MockTransport();
    auto client = make_connected_client(transport);

    ea::ToolSpec spec{"safe_tool", "Safe", nlohmann::json::object()};
    McpToolAdapter adapter(client, spec);

    REQUIRE(adapter.is_dangerous() == false);
}

TEST_CASE("McpClient disconnect stops transport", "[mcp]") {
    auto transport = new MockTransport();
    transport->responses.push_back(init_result());
    transport->responses.push_back({{"jsonrpc", "2.0"}, {"id", 2}, {"result", nullptr}});

    auto client = std::make_shared<McpClient>(std::unique_ptr<ITransport>(transport));
    client->connect();
    REQUIRE(client->is_connected());

    client->disconnect();
    REQUIRE_FALSE(client->is_connected());
    REQUIRE_FALSE(transport->is_running());
}
