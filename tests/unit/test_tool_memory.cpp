#include <catch2/catch_test_macros.hpp>
#include "tool/MemoryTool.h"
#include "memory/IMemory.h"
#include "base/Types.h"

using namespace ea;
using namespace ea::tool;

// Mock IMemory for testing MemoryTool
class MockMemory : public IMemory {
public:
    Result<std::string> store(const std::string& content,
                               const std::string& category = "core",
                               int importance = 5) override {
        stored_.push_back({content, category, importance});
        return "mock-id-" + std::to_string(stored_.size());
    }

    Result<std::vector<MemoryEntry>> recall(const std::string& query, int /*limit*/ = 10) override {
        if (query == "fail") {
            return Error::db("recall failed");
        }
        MemoryEntry entry;
        entry.id = "mock-id-1";
        entry.content = "mock content for " + query;
        entry.category = "core";
        entry.importance = 5;
        entry.created_at = "2026-01-01T00:00:00Z";
        return std::vector<MemoryEntry>{entry};
    }

    Result<bool> forget(const std::string& id) override {
        if (id == "not-found") return false;
        if (id == "fail") return Error::db("forget failed");
        forgotten_.push_back(id);
        return true;
    }

    Result<std::vector<MemoryEntry>> list(int /*limit*/ = 50, int /*offset*/ = 0) override {
        return std::vector<MemoryEntry>{};
    }

    Result<int> count() override { return 0; }

    // Test inspection
    std::vector<std::tuple<std::string, std::string, int>> stored_;
    std::vector<std::string> forgotten_;
};

TEST_CASE("MemoryTool name is memory", "[tool][memory]") {
    MockMemory mem;
    MemoryTool tool(&mem);
    REQUIRE(tool.name() == "memory");
}

TEST_CASE("MemoryTool store action", "[tool][memory]") {
    MockMemory mem;
    MemoryTool tool(&mem);

    auto result = tool.execute(json{{"action", "store"}, {"content", "test content"}, {"category", "daily"}, {"importance", 8}});
    REQUIRE(result.ok());
    REQUIRE(result.value().output.find("mock-id-") != std::string::npos);
    REQUIRE(result.value().is_error == false);
    REQUIRE(mem.stored_.size() == 1);
    REQUIRE(std::get<0>(mem.stored_[0]) == "test content");
    REQUIRE(std::get<1>(mem.stored_[0]) == "daily");
    REQUIRE(std::get<2>(mem.stored_[0]) == 8);
}

TEST_CASE("MemoryTool store with defaults", "[tool][memory]") {
    MockMemory mem;
    MemoryTool tool(&mem);

    auto result = tool.execute(json{{"action", "store"}, {"content", "minimal"}});
    REQUIRE(result.ok());
    REQUIRE(std::get<1>(mem.stored_[0]) == "core");   // default category
    REQUIRE(std::get<2>(mem.stored_[0]) == 5);         // default importance
}

TEST_CASE("MemoryTool recall action", "[tool][memory]") {
    MockMemory mem;
    MemoryTool tool(&mem);

    auto result = tool.execute(json{{"action", "recall"}, {"query", "test query"}});
    REQUIRE(result.ok());
    REQUIRE(result.value().is_error == false);
    // Output should be JSON array
    auto parsed = json::parse(result.value().output);
    REQUIRE(parsed.size() == 1);
    REQUIRE(parsed[0]["content"] == "mock content for test query");
}

TEST_CASE("MemoryTool forget action", "[tool][memory]") {
    MockMemory mem;
    MemoryTool tool(&mem);

    auto result = tool.execute(json{{"action", "forget"}, {"id", "some-id"}});
    REQUIRE(result.ok());
    REQUIRE(result.value().output.find("Forgotten") != std::string::npos);
    REQUIRE(mem.forgotten_.size() == 1);
}

TEST_CASE("MemoryTool forget not found", "[tool][memory]") {
    MockMemory mem;
    MemoryTool tool(&mem);

    auto result = tool.execute(json{{"action", "forget"}, {"id", "not-found"}});
    REQUIRE(result.ok());
    REQUIRE(result.value().output.find("Not found") != std::string::npos);
}

TEST_CASE("MemoryTool missing action parameter", "[tool][memory]") {
    MockMemory mem;
    MemoryTool tool(&mem);

    auto result = tool.execute(json{{"content", "test"}});
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("MemoryTool unknown action", "[tool][memory]") {
    MockMemory mem;
    MemoryTool tool(&mem);

    auto result = tool.execute(json{{"action", "invalid"}});
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("MemoryTool store missing content", "[tool][memory]") {
    MockMemory mem;
    MemoryTool tool(&mem);

    auto result = tool.execute(json{{"action", "store"}});
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("MemoryTool recall missing query", "[tool][memory]") {
    MockMemory mem;
    MemoryTool tool(&mem);

    auto result = tool.execute(json{{"action", "recall"}});
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("MemoryTool forget missing id", "[tool][memory]") {
    MockMemory mem;
    MemoryTool tool(&mem);

    auto result = tool.execute(json{{"action", "forget"}});
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("MemoryTool null memory backend", "[tool][memory]") {
    MemoryTool tool(nullptr);

    auto result = tool.execute(json{{"action", "store"}, {"content", "test"}});
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ErrorCode::ToolError);
}
