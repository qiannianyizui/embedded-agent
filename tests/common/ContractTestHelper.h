// tests/common/ContractTestHelper.h
// Contract testing infrastructure — verifies behavioral contracts for core
// interfaces (IProvider, ITool, IMemory) through their public API only.
// Grey-box: uses interface knowledge but tests through public methods.
#pragma once
#include "provider/IProvider.h"
#include "tool/ITool.h"
#include "memory/IMemory.h"
#include "core/Types.h"
#include "common/base/Result.h"
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>
#include <functional>

namespace ea::test::contract {

// ── ProviderContract ──────────────────────────────────────────────────────────
// Verifies that an IProvider implementation satisfies the behavioral contract:
//   1. name() returns a non-empty string
//   2. capabilities() returns valid ProviderCapabilities
//   3. chat() with valid input returns Result<LLMResponse> (ok or error)
//   4. stream_chat() with valid input returns Result<void> (ok or error)
//   5. chat() never throws (errors go through Result)
//   6. stream_chat() never throws (errors go through Result)

class ProviderContract {
public:
    // Verify all contract invariants for the given provider.
    // The provider should be pre-configured with a queued response.
    static void verify_all(IProvider& provider) {
        verify_name(provider);
        verify_capabilities(provider);
        verify_chat_returns_result(provider);
        verify_stream_chat_returns_result(provider);
        verify_chat_no_throw(provider);
        verify_stream_chat_no_throw(provider);
    }

    static void verify_name(IProvider& provider) {
        INFO("ProviderContract: name() must return non-empty string");
        REQUIRE_FALSE(provider.name().empty());
    }

    static void verify_capabilities(IProvider& provider) {
        INFO("ProviderContract: capabilities() must not throw and return struct");
        // capabilities() must be callable without throwing
        REQUIRE_NOTHROW(provider.capabilities());
    }

    static void verify_chat_returns_result(IProvider& provider) {
        INFO("ProviderContract: chat() must return Result (ok or error) without throwing");
        std::vector<Message> msgs = {
            Message{Role::User, "contract test", std::nullopt, std::nullopt, std::nullopt}
        };
        auto result = provider.chat(msgs, {}, "model");
        // Must return a valid Result — either ok with LLMResponse or error with ErrorCode
        if (!result.ok()) {
            REQUIRE(result.error().code != ErrorCode{});
        }
    }

    static void verify_stream_chat_returns_result(IProvider& provider) {
        INFO("ProviderContract: stream_chat() must return Result (ok or error)");
        std::vector<Message> msgs = {
            Message{Role::User, "contract test", std::nullopt, std::nullopt, std::nullopt}
        };
        int chunk_count = 0;
        auto result = provider.stream_chat(msgs, {}, "model",
            [&](const StreamChunk&) { chunk_count++; }, {});
        // Must return a valid Result — either ok or error with ErrorCode
        if (!result.ok()) {
            REQUIRE(result.error().code != ErrorCode{});
        }
    }

    static void verify_chat_no_throw(IProvider& provider) {
        INFO("ProviderContract: chat() must not throw");
        std::vector<Message> msgs = {
            Message{Role::User, "contract test", std::nullopt, std::nullopt, std::nullopt}
        };
        REQUIRE_NOTHROW(provider.chat(msgs, {}, "model"));
    }

    static void verify_stream_chat_no_throw(IProvider& provider) {
        INFO("ProviderContract: stream_chat() must not throw");
        std::vector<Message> msgs = {
            Message{Role::User, "contract test", std::nullopt, std::nullopt, std::nullopt}
        };
        REQUIRE_NOTHROW(provider.stream_chat(msgs, {}, "model",
            [](const StreamChunk&) {}, {}));
    }
};

// ── ToolContract ──────────────────────────────────────────────────────────────
// Verifies that an ITool implementation satisfies the behavioral contract:
//   1. name() returns a non-empty string
//   2. description() returns a non-empty string
//   3. parameters_schema() returns valid JSON object
//   4. execute() with valid args returns Result<ToolResult> (ok or error)
//   5. is_mutating() and is_dangerous() return bool (no throw)
//   6. execute() never throws (errors go through Result)

class ToolContract {
public:
    static void verify_all(ITool& tool) {
        verify_name(tool);
        verify_description(tool);
        verify_parameters_schema(tool);
        verify_execute_returns_result(tool);
        verify_metadata_no_throw(tool);
        verify_execute_no_throw(tool);
    }

    static void verify_name(ITool& tool) {
        INFO("ToolContract: name() must return non-empty string");
        REQUIRE_FALSE(tool.name().empty());
    }

    static void verify_description(ITool& tool) {
        INFO("ToolContract: description() must return non-empty string");
        REQUIRE_FALSE(tool.description().empty());
    }

    static void verify_parameters_schema(ITool& tool) {
        INFO("ToolContract: parameters_schema() must return valid JSON object");
        auto schema = tool.parameters_schema();
        REQUIRE(schema.is_object());
    }

    static void verify_execute_returns_result(ITool& tool) {
        INFO("ToolContract: execute() must return Result (ok or error) without throwing");
        json args = json::object();
        auto result = tool.execute(args);
        // Must be either ok or error
        if (result.ok()) {
            // ToolResult should have meaningful output or be an error marker
            REQUIRE((!result.value().output.empty() || result.value().is_error));
        }
    }

    static void verify_metadata_no_throw(ITool& tool) {
        INFO("ToolContract: is_mutating()/is_dangerous() must not throw");
        REQUIRE_NOTHROW(tool.is_mutating());
        REQUIRE_NOTHROW(tool.is_dangerous());
    }

    static void verify_execute_no_throw(ITool& tool) {
        INFO("ToolContract: execute() must not throw");
        json args = json::object();
        REQUIRE_NOTHROW(tool.execute(args));
    }
};

// ── MemoryContract ────────────────────────────────────────────────────────────
// Verifies that an IMemory implementation satisfies the behavioral contract:
//   1. store() returns Result<string> (id or error)
//   2. recall() returns Result<vector<MemoryEntry>> (entries or error)
//   3. forget() returns Result<bool> (existed or error)
//   4. list() returns Result<vector<MemoryEntry>> (entries or error)
//   5. count() returns Result<int> (count or error)
//   6. store→recall roundtrip: stored content is retrievable
//   7. open()/close() do not throw

class MemoryContract {
public:
    static void verify_all(IMemory& memory) {
        verify_open_close(memory);
        verify_store_returns_result(memory);
        verify_recall_returns_result(memory);
        verify_forget_returns_result(memory);
        verify_list_returns_result(memory);
        verify_count_returns_result(memory);
        verify_store_recall_roundtrip(memory);
    }

    static void verify_open_close(IMemory& memory) {
        INFO("MemoryContract: open()/close() must not throw");
        REQUIRE_NOTHROW(memory.open());
        REQUIRE_NOTHROW(memory.close());
    }

    static void verify_store_returns_result(IMemory& memory) {
        INFO("MemoryContract: store() must return Result<string>");
        auto result = memory.store("contract test content", "contract", 5);
        if (result.ok()) {
            REQUIRE_FALSE(result.value().empty());
        }
    }

    static void verify_recall_returns_result(IMemory& memory) {
        INFO("MemoryContract: recall() must return Result<vector<MemoryEntry>>");
        auto result = memory.recall("contract test", 10);
        REQUIRE(result.ok());
    }

    static void verify_forget_returns_result(IMemory& memory) {
        INFO("MemoryContract: forget() must return Result<bool>");
        // Forget non-existent ID
        auto result = memory.forget("nonexistent-id-contract-test");
        // Forget of non-existent ID should return ok(false) or error
        if (result.ok()) {
            REQUIRE_FALSE(result.value());  // didn't exist
        }
    }

    static void verify_list_returns_result(IMemory& memory) {
        INFO("MemoryContract: list() must return Result<vector<MemoryEntry>>");
        auto result = memory.list(10, 0);
        REQUIRE(result.ok());
    }

    static void verify_count_returns_result(IMemory& memory) {
        INFO("MemoryContract: count() must return Result<int>");
        auto result = memory.count();
        REQUIRE(result.ok());
        REQUIRE(result.value() >= 0);
    }

    static void verify_store_recall_roundtrip(IMemory& memory) {
        INFO("MemoryContract: store→recall roundtrip must retrieve stored content");
        // Store a unique entry
        std::string unique_content = "contract_roundtrip_" + std::to_string(reinterpret_cast<uintptr_t>(&memory));
        auto store_result = memory.store(unique_content, "roundtrip", 7);
        REQUIRE(store_result.ok());
        std::string id = store_result.value();
        REQUIRE_FALSE(id.empty());

        // Recall should find it
        auto recall_result = memory.recall(unique_content, 10);
        REQUIRE(recall_result.ok());

        bool found = false;
        for (const auto& entry : recall_result.value()) {
            if (entry.id == id) {
                found = true;
                REQUIRE(entry.content == unique_content);
                REQUIRE(entry.category == "roundtrip");
                REQUIRE(entry.importance == 7);
                break;
            }
        }
        REQUIRE(found);

        // Clean up
        memory.forget(id);
    }
};

}  // namespace ea::test::contract
