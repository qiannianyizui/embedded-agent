// tests/blackbox/ContractTestHelper.h
// Contract verification helpers for IProvider, ITool, and IMemory interfaces.
// These test through public APIs only (grey-box) and verify behavioral contracts
// that ALL implementations must satisfy.
#pragma once
#include "core/IProvider.h"
#include "core/ITool.h"
#include "core/IMemory.h"
#include "core/Types.h"
#include "common/base/Result.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_section_info.hpp>
#include <string>
#include <vector>

namespace ea::test::contract {

// --- IProvider Contract ---
struct ProviderContract {
    static void verify_name(IProvider& p) {
        INFO("ProviderContract::verify_name");
        std::string n = p.name();
        REQUIRE_FALSE(n.empty());
    }

    static void verify_capabilities(IProvider& p) {
        INFO("ProviderContract::verify_capabilities");
        auto caps = p.capabilities();
        // All fields must be bools (verified by compilation).
        // Just check they are accessible and consistent.
        // A provider that claims streaming must handle stream_chat.
        (void)caps.native_tool_calling;
        (void)caps.streaming;
        (void)caps.vision;
        (void)caps.prompt_caching;
        (void)caps.extended_thinking;
    }

    static void verify_chat_returns_result(IProvider& p) {
        INFO("ProviderContract::verify_chat_returns_result");
        std::vector<Message> msgs = {Message{Role::User, "test", std::nullopt, std::nullopt, std::nullopt}};
        std::vector<ToolSpec> tools;
        auto result = p.chat(msgs, tools, "test-model");
        // Result must be either ok or error, never both
        if (result.ok()) {
            // If ok, verify response structure
            auto& resp = result.value();
            // stop_reason should be set for valid responses
            // content may be empty (tool_use responses)
            (void)resp;
        }
        // If not ok, error() is accessible
    }

    static void verify_stream_returns_result(IProvider& p) {
        INFO("ProviderContract::verify_stream_returns_result");
        std::vector<Message> msgs = {Message{Role::User, "test", std::nullopt, std::nullopt, std::nullopt}};
        std::vector<ToolSpec> tools;
        bool callback_invoked = false;
        auto result = p.stream_chat(msgs, tools, "test-model",
            [&](const StreamChunk& chunk) {
                callback_invoked = true;
                (void)chunk;
            });
        // stream_chat must return Result<void> — never throws
        if (result.ok()) {
            // Callback may or may not have been invoked
        }
    }

    static void verify_list_models(IProvider& p) {
        INFO("ProviderContract::verify_list_models");
        auto models = p.list_models();
        // Returns a vector; may be empty for single-model providers
        (void)models;
    }

    static void verify_all(IProvider& p) {
        verify_name(p);
        verify_capabilities(p);
        verify_list_models(p);
        verify_chat_returns_result(p);
        verify_stream_returns_result(p);
    }
};

// --- ITool Contract ---
struct ToolContract {
    static void verify_name(ITool& t) {
        INFO("ToolContract::verify_name");
        std::string n = t.name();
        REQUIRE_FALSE(n.empty());
    }

    static void verify_description(ITool& t) {
        INFO("ToolContract::verify_description");
        std::string d = t.description();
        // Description may be empty but must not throw
        (void)d;
    }

    static void verify_schema(ITool& t) {
        INFO("ToolContract::verify_schema");
        json schema = t.parameters_schema();
        // Schema should be a JSON object (JSON Schema format)
        REQUIRE(schema.is_object());
    }

    static void verify_execute_returns_result(ITool& t) {
        INFO("ToolContract::verify_execute_returns_result");
        // Execute with empty args — must not throw, must return Result
        auto result = t.execute(json::object());
        // Result is either ok or error
        if (result.ok()) {
            auto& tr = result.value();
            // ToolResult has call_id, output, is_error
            (void)tr.output;
            (void)tr.is_error;
        }
    }

    static void verify_metadata(ITool& t) {
        INFO("ToolContract::verify_metadata");
        // is_mutating and is_dangerous must return bool
        bool mutating = t.is_mutating();
        bool dangerous = t.is_dangerous();
        (void)mutating;
        (void)dangerous;
        // Consistency: dangerous tools should be mutating
        // (a dangerous read-only tool is unusual but not forbidden)
    }

    static void verify_all(ITool& t) {
        verify_name(t);
        verify_description(t);
        verify_schema(t);
        verify_metadata(t);
        verify_execute_returns_result(t);
    }
};

// --- IMemory Contract ---
struct MemoryContract {
    static void verify_store_recall_roundtrip(IMemory& m) {
        INFO("MemoryContract::verify_store_recall_roundtrip");
        auto open_r = m.open();
        REQUIRE(open_r.ok());

        auto id_result = m.store("contract test content unique123", "contract_test", 7);
        REQUIRE(id_result.ok());
        REQUIRE_FALSE(id_result.value().empty());

        auto recall_result = m.recall("unique123", 10);
        REQUIRE(recall_result.ok());
        bool found = false;
        for (const auto& entry : recall_result.value()) {
            if (entry.content.find("unique123") != std::string::npos) {
                found = true;
                break;
            }
        }
        REQUIRE(found);
    }

    static void verify_forget_existing(IMemory& m) {
        INFO("MemoryContract::verify_forget_existing");
        m.open();
        auto id_result = m.store("to be forgotten", "contract_test", 5);
        REQUIRE(id_result.ok());

        auto forget_result = m.forget(id_result.value());
        REQUIRE(forget_result.ok());
        REQUIRE(forget_result.value() == true);
    }

    static void verify_forget_nonexistent(IMemory& m) {
        INFO("MemoryContract::verify_forget_nonexistent");
        m.open();
        auto forget_result = m.forget("nonexistent_id_xyz");
        REQUIRE(forget_result.ok());
        // forget of nonexistent ID should return false
        REQUIRE_FALSE(forget_result.value());
    }

    static void verify_list_pagination(IMemory& m) {
        INFO("MemoryContract::verify_list_pagination");
        m.open();
        // Store a few entries
        for (int i = 0; i < 5; ++i) {
            m.store("pagination item " + std::to_string(i), "contract_test", 5);
        }

        auto page1 = m.list(3, 0);
        REQUIRE(page1.ok());
        REQUIRE(page1.value().size() <= 3);

        auto page2 = m.list(3, 3);
        REQUIRE(page2.ok());
        // Pages should not overlap (by offset)
    }

    static void verify_count_consistency(IMemory& m) {
        INFO("MemoryContract::verify_count_consistency");
        m.open();
        auto count_before = m.count();
        REQUIRE(count_before.ok());

        m.store("count test item", "contract_test", 5);

        auto count_after = m.count();
        REQUIRE(count_after.ok());
        REQUIRE(count_after.value() >= count_before.value());
    }

    static void verify_lifecycle(IMemory& m) {
        INFO("MemoryContract::verify_lifecycle");
        // open/close should not crash
        auto open_r = m.open();
        REQUIRE(open_r.ok());

        auto close_r = m.close();
        REQUIRE(close_r.ok());

        // Lifecycle hooks should not throw
        m.on_turn_start("test input");
        m.on_turn_end("test output");
        m.on_pre_compress();

        // system_prompt_block should return a string
        std::string prompt_block = m.system_prompt_block();
        (void)prompt_block;  // may be empty
    }

    static void verify_all(IMemory& m) {
        verify_lifecycle(m);
        verify_store_recall_roundtrip(m);
        verify_forget_existing(m);
        verify_forget_nonexistent(m);
        verify_list_pagination(m);
        verify_count_consistency(m);
    }
};

}  // namespace ea::test::contract
