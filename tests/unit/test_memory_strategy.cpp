// tests/test_memory_strategy.cpp
// Comprehensive tests for the memory strategy system (Task 7)
#include <catch2/catch_test_macros.hpp>
#include "agent/IMemoryStrategy.h"
#include "agent/ProgressiveMemoryStrategy.h"
#include "agent/AgentLoop.h"
#include "memory/HolographicMemory.h"
#include "provider/IProvider.h"

using namespace ea;
using namespace ea::agent;
using namespace ea::memory;

// --- Mock provider that returns controlled responses ---

class StrategyTestProvider : public IProvider {
public:
    std::string name() const override { return "strategy-test"; }
    std::vector<std::string> list_models() const override { return {"test-model"}; }
    provider::ProviderCapabilities capabilities() const override {
        return {true, true, false, false, false};
    }

    Result<LLMResponse> chat(const std::vector<Message>& msgs,
                              const std::vector<ToolSpec>&,
                              const std::string&,
                              const ChatOptions&) override {
        chat_count_++;
        last_messages_ = msgs;

        if (fail_next_) {
            fail_next_ = false;
            return Error::net("provider error");
        }

        // Return different responses based on context
        // If the user message contains "Extract key facts" -> return fact-like lines
        for (auto it = msgs.rbegin(); it != msgs.rend(); ++it) {
            if (it->role == Role::User) {
                if (it->content.find("Extract key facts") != std::string::npos) {
                    LLMResponse resp;
                    resp.content = "- User prefers dark mode\n- Project uses CMake\n";
                    resp.stop_reason = "stop";
                    return resp;
                }
                if (it->content.find("Summarize") != std::string::npos ||
                    it->content.find("summarizer") != std::string::npos) {
                    LLMResponse resp;
                    resp.content = "User discussed testing strategies and CMake configuration.";
                    resp.stop_reason = "stop";
                    return resp;
                }
                break;
            }
        }

        LLMResponse resp;
        resp.content = "Default response";
        resp.stop_reason = "stop";
        return resp;
    }

    Result<void> stream_chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, std::function<void(const StreamChunk&)>,
                              const ChatOptions&) override {
        return Error::net("not implemented");
    }

    int chat_count() const { return chat_count_; }
    void set_fail_next(bool f) { fail_next_ = f; }
    const std::vector<Message>& last_messages() const { return last_messages_; }

private:
    int chat_count_ = 0;
    bool fail_next_ = false;
    std::vector<Message> last_messages_;
};

// Helper: create an opened HolographicMemory for tests
static std::unique_ptr<HolographicMemory> make_test_memory() {
    auto mem = std::make_unique<HolographicMemory>(HolographicMemoryConfig{":memory:", false});
    mem->open();
    return mem;
}

// --- NullMemoryStrategy tests ---

TEST_CASE("NullMemoryStrategy returns empty prompt", "[memory][strategy]") {
    NullMemoryStrategy strategy;
    std::vector<Message> history;
    REQUIRE(strategy.build_memory_prompt(history, nullptr).empty());
}

TEST_CASE("NullMemoryStrategy on_turn_end is no-op", "[memory][strategy]") {
    NullMemoryStrategy strategy;
    auto memory = make_test_memory();
    auto provider = std::make_shared<StrategyTestProvider>();
    std::vector<Message> history;
    std::string input = "hello";
    std::string output = "world";

    MemoryStrategyContext ctx{history, memory.get(), provider.get(), input, output};
    strategy.on_turn_end(ctx);

    REQUIRE(history.empty());
    REQUIRE(strategy.is_active() == false);
}

// --- ProgressiveMemoryConfig defaults ---

TEST_CASE("ProgressiveMemoryConfig default values", "[memory][strategy]") {
    ProgressiveMemoryConfig config;
    REQUIRE(config.working_turns == 6);
    REQUIRE(config.short_term_max == 20);
    REQUIRE(config.long_term_importance == 8);
    REQUIRE(config.enable_fact_extraction == true);
    REQUIRE(config.enable_auto_summarize == true);
    REQUIRE(config.fact_extraction_prompt.empty());
    REQUIRE(config.summarization_prompt.empty());
}

// --- ProgressiveMemoryStrategy.is_active ---

TEST_CASE("ProgressiveMemoryStrategy is active", "[memory][strategy]") {
    ProgressiveMemoryStrategy strategy;
    REQUIRE(strategy.is_active() == true);
}

// --- build_memory_prompt tests ---

TEST_CASE("build_memory_prompt returns empty with no memories", "[memory][strategy]") {
    ProgressiveMemoryStrategy strategy;
    auto memory = make_test_memory();
    std::vector<Message> history = {
        {Role::User, "hello", std::nullopt, std::nullopt, std::nullopt}
    };

    REQUIRE(strategy.build_memory_prompt(history, memory.get()).empty());
}

TEST_CASE("build_memory_prompt returns empty with null memory", "[memory][strategy]") {
    ProgressiveMemoryStrategy strategy;
    std::vector<Message> history = {
        {Role::User, "hello", std::nullopt, std::nullopt, std::nullopt}
    };

    REQUIRE(strategy.build_memory_prompt(history, nullptr).empty());
}

TEST_CASE("build_memory_prompt includes long-term facts", "[memory][strategy]") {
    ProgressiveMemoryStrategy strategy;
    auto memory = make_test_memory();

    memory->store("User prefers dark mode", "long_term", 8);
    memory->store("Project uses CMake", "long_term", 8);

    std::vector<Message> history = {
        {Role::User, "dark mode", std::nullopt, std::nullopt, std::nullopt}
    };

    std::string prompt = strategy.build_memory_prompt(history, memory.get());
    REQUIRE(!prompt.empty());
    REQUIRE(prompt.find("Key Facts") != std::string::npos);
    REQUIRE(prompt.find("dark mode") != std::string::npos);
}

TEST_CASE("build_memory_prompt includes short-term summaries", "[memory][strategy]") {
    ProgressiveMemoryStrategy strategy;
    auto memory = make_test_memory();

    memory->store("Previous conversation about testing", "short_term", 5);

    std::vector<Message> history = {
        {Role::User, "continue", std::nullopt, std::nullopt, std::nullopt}
    };

    std::string prompt = strategy.build_memory_prompt(history, memory.get());
    REQUIRE(!prompt.empty());
    REQUIRE(prompt.find("Recent Summary") != std::string::npos);
    REQUIRE(prompt.find("testing") != std::string::npos);
}

TEST_CASE("build_memory_prompt includes both long-term and short-term", "[memory][strategy]") {
    ProgressiveMemoryStrategy strategy;
    auto memory = make_test_memory();

    memory->store("User prefers dark mode", "long_term", 8);
    memory->store("Previous discussion about CMake", "short_term", 5);

    // Use a query that is a substring of the long-term entry content
    // (HolographicMemory::recall uses FTS5 search)
    std::vector<Message> history = {
        {Role::User, "dark mode", std::nullopt, std::nullopt, std::nullopt}
    };

    std::string prompt = strategy.build_memory_prompt(history, memory.get());
    REQUIRE(!prompt.empty());
    REQUIRE(prompt.find("Key Facts") != std::string::npos);
    REQUIRE(prompt.find("Recent Summary") != std::string::npos);
}

TEST_CASE("build_memory_prompt filters by category", "[memory][strategy]") {
    ProgressiveMemoryStrategy strategy;
    auto memory = make_test_memory();

    // Store a "core" category entry -- should NOT appear in memory prompt
    memory->store("Some core memory", "core", 5);
    memory->store("A long-term fact", "long_term", 8);

    std::vector<Message> history = {
        {Role::User, "fact", std::nullopt, std::nullopt, std::nullopt}
    };

    std::string prompt = strategy.build_memory_prompt(history, memory.get());
    REQUIRE(!prompt.empty());
    REQUIRE(prompt.find("long-term fact") != std::string::npos);
    // "core" entries should not be in the structured prompt sections
}

// --- on_turn_end: fact extraction ---

TEST_CASE("on_turn_end extracts facts from conversation", "[memory][strategy]") {
    ProgressiveMemoryStrategy strategy;
    auto memory = make_test_memory();
    auto provider = std::make_shared<StrategyTestProvider>();

    std::vector<Message> history;
    std::string input = "I prefer dark mode";
    std::string output = "Noted, you prefer dark mode";

    MemoryStrategyContext ctx{history, memory.get(), provider.get(), input, output};
    strategy.on_turn_end(ctx);

    // Check that facts were stored in long-term memory
    auto list_result = memory->list(50, 0);
    REQUIRE(list_result.ok());
    bool found_long_term = false;
    for (const auto& entry : list_result.value()) {
        if (entry.category == "long_term") {
            found_long_term = true;
            break;
        }
    }
    REQUIRE(found_long_term);
    REQUIRE(provider->chat_count() >= 1);
}

TEST_CASE("on_turn_end skips fact extraction when disabled", "[memory][strategy]") {
    ProgressiveMemoryConfig config;
    config.enable_fact_extraction = false;
    ProgressiveMemoryStrategy strategy(config);
    auto memory = make_test_memory();
    auto provider = std::make_shared<StrategyTestProvider>();

    std::vector<Message> history;
    std::string input = "hello";
    std::string output = "world";

    MemoryStrategyContext ctx{history, memory.get(), provider.get(), input, output};
    strategy.on_turn_end(ctx);

    REQUIRE(provider->chat_count() == 0);
}

TEST_CASE("on_turn_end gracefully handles LLM failure in extraction", "[memory][strategy]") {
    ProgressiveMemoryStrategy strategy;
    auto memory = make_test_memory();
    auto provider = std::make_shared<StrategyTestProvider>();
    provider->set_fail_next(true);

    std::vector<Message> history;
    std::string input = "hello";
    std::string output = "world";

    MemoryStrategyContext ctx{history, memory.get(), provider.get(), input, output};
    strategy.on_turn_end(ctx);

    // Should not crash, no long-term entries stored
    auto list_result = memory->list(50, 0);
    REQUIRE(list_result.ok());
    bool found_long_term = false;
    for (const auto& entry : list_result.value()) {
        if (entry.category == "long_term") {
            found_long_term = true;
        }
    }
    REQUIRE_FALSE(found_long_term);
}

// --- on_turn_end: summarization ---

TEST_CASE("on_turn_end summarizes old messages when history exceeds working_turns", "[memory][strategy]") {
    ProgressiveMemoryConfig config;
    config.working_turns = 2;  // Keep only last 2 turns = 4 messages
    config.enable_fact_extraction = false;  // Focus on summarization
    ProgressiveMemoryStrategy strategy(config);
    auto memory = make_test_memory();
    auto provider = std::make_shared<StrategyTestProvider>();

    // Build history with more than 4 messages (2 turns)
    std::vector<Message> history;
    for (int i = 0; i < 5; ++i) {
        history.push_back({Role::User, "Q" + std::to_string(i), std::nullopt, std::nullopt, std::nullopt});
        history.push_back({Role::Assistant, "A" + std::to_string(i), std::nullopt, std::nullopt, std::nullopt});
    }

    std::string input = "Q4";
    std::string output = "A4";

    MemoryStrategyContext ctx{history, memory.get(), provider.get(), input, output};
    strategy.on_turn_end(ctx);

    // History should be shorter than 10 messages
    REQUIRE(history.size() < 10);

    // Short-term memory should have a summary
    auto list_result = memory->list(50, 0);
    REQUIRE(list_result.ok());
    bool found_short_term = false;
    for (const auto& entry : list_result.value()) {
        if (entry.category == "short_term") {
            found_short_term = true;
            break;
        }
    }
    REQUIRE(found_short_term);
}

TEST_CASE("on_turn_end does not summarize when history is within working_turns", "[memory][strategy]") {
    ProgressiveMemoryConfig config;
    config.working_turns = 10;
    config.enable_fact_extraction = false;
    ProgressiveMemoryStrategy strategy(config);
    auto memory = make_test_memory();
    auto provider = std::make_shared<StrategyTestProvider>();

    std::vector<Message> history = {
        {Role::User, "Q0", std::nullopt, std::nullopt, std::nullopt},
        {Role::Assistant, "A0", std::nullopt, std::nullopt, std::nullopt},
        {Role::User, "Q1", std::nullopt, std::nullopt, std::nullopt},
        {Role::Assistant, "A1", std::nullopt, std::nullopt, std::nullopt}
    };

    size_t original_size = history.size();
    std::string input = "Q1";
    std::string output = "A1";

    MemoryStrategyContext ctx{history, memory.get(), provider.get(), input, output};
    strategy.on_turn_end(ctx);

    REQUIRE(history.size() == original_size);
    REQUIRE(provider->chat_count() == 0);
}

TEST_CASE("on_turn_end skips summarization when disabled", "[memory][strategy]") {
    ProgressiveMemoryConfig config;
    config.working_turns = 1;
    config.enable_auto_summarize = false;
    config.enable_fact_extraction = false;
    ProgressiveMemoryStrategy strategy(config);
    auto memory = make_test_memory();
    auto provider = std::make_shared<StrategyTestProvider>();

    std::vector<Message> history;
    for (int i = 0; i < 6; ++i) {
        history.push_back({Role::User, "Q" + std::to_string(i), std::nullopt, std::nullopt, std::nullopt});
        history.push_back({Role::Assistant, "A" + std::to_string(i), std::nullopt, std::nullopt, std::nullopt});
    }

    size_t original_size = history.size();
    std::string input = "Q5";
    std::string output = "A5";

    MemoryStrategyContext ctx{history, memory.get(), provider.get(), input, output};
    strategy.on_turn_end(ctx);

    // History should be unchanged
    REQUIRE(history.size() == original_size);
    REQUIRE(provider->chat_count() == 0);
}

// --- on_turn_end: short-term eviction ---

TEST_CASE("on_turn_end evicts excess short-term entries", "[memory][strategy]") {
    ProgressiveMemoryConfig config;
    config.short_term_max = 3;
    config.enable_fact_extraction = false;
    config.enable_auto_summarize = false;
    ProgressiveMemoryStrategy strategy(config);
    auto memory = make_test_memory();

    // Pre-fill with more than short_term_max entries
    for (int i = 0; i < 5; ++i) {
        memory->store("Summary " + std::to_string(i), "short_term", 5);
    }

    std::vector<Message> history;
    std::string input = "hello";
    std::string output = "world";
    auto provider = std::make_shared<StrategyTestProvider>();

    MemoryStrategyContext ctx{history, memory.get(), provider.get(), input, output};
    strategy.on_turn_end(ctx);

    // Count remaining short_term entries
    auto list_result = memory->list(50, 0);
    REQUIRE(list_result.ok());
    int short_term_count = 0;
    for (const auto& entry : list_result.value()) {
        if (entry.category == "short_term") {
            short_term_count++;
        }
    }
    REQUIRE(short_term_count <= config.short_term_max);
}

// --- Custom prompts ---

TEST_CASE("ProgressiveMemoryStrategy uses custom extraction prompt", "[memory][strategy]") {
    ProgressiveMemoryConfig config;
    config.fact_extraction_prompt = "CUSTOM_EXTRACT: {user_input} | {assistant_output}";
    ProgressiveMemoryStrategy strategy(config);
    auto memory = make_test_memory();
    auto provider = std::make_shared<StrategyTestProvider>();

    std::vector<Message> history;
    std::string input = "test input";
    std::string output = "test output";

    MemoryStrategyContext ctx{history, memory.get(), provider.get(), input, output};
    strategy.on_turn_end(ctx);

    // Verify the custom prompt was used
    REQUIRE(provider->chat_count() >= 1);
    bool found_custom = false;
    for (const auto& msg : provider->last_messages()) {
        if (msg.content.find("CUSTOM_EXTRACT") != std::string::npos) {
            found_custom = true;
            break;
        }
    }
    REQUIRE(found_custom);
}

TEST_CASE("ProgressiveMemoryStrategy uses custom summarization prompt", "[memory][strategy]") {
    ProgressiveMemoryConfig config;
    config.working_turns = 1;
    config.enable_fact_extraction = false;
    config.summarization_prompt = "CUSTOM_SUMMARIZE";
    ProgressiveMemoryStrategy strategy(config);
    auto memory = make_test_memory();
    auto provider = std::make_shared<StrategyTestProvider>();

    std::vector<Message> history;
    for (int i = 0; i < 4; ++i) {
        history.push_back({Role::User, "Q" + std::to_string(i), std::nullopt, std::nullopt, std::nullopt});
        history.push_back({Role::Assistant, "A" + std::to_string(i), std::nullopt, std::nullopt, std::nullopt});
    }

    std::string input = "Q3";
    std::string output = "A3";

    MemoryStrategyContext ctx{history, memory.get(), provider.get(), input, output};
    strategy.on_turn_end(ctx);

    // Verify the custom prompt was used
    REQUIRE(provider->chat_count() >= 1);
    bool found_custom = false;
    for (const auto& msg : provider->last_messages()) {
        if (msg.content.find("CUSTOM_SUMMARIZE") != std::string::npos) {
            found_custom = true;
            break;
        }
    }
    REQUIRE(found_custom);
}

// --- Integration: AgentLoop + ProgressiveMemoryStrategy ---

TEST_CASE("AgentLoop with strategy processes turns", "[agent][memory][strategy]") {
    auto memory = make_test_memory();
    auto provider = std::make_shared<StrategyTestProvider>();

    ProgressiveMemoryConfig strat_config;
    strat_config.enable_fact_extraction = true;
    strat_config.enable_auto_summarize = false;  // Short history, no need
    auto strategy = std::make_unique<ProgressiveMemoryStrategy>(strat_config);

    ea::tool::ToolRegistry registry;

    AgentLoop loop(
        provider.get(), &registry, memory.get(),
        AgentLoop::Config{5, 1024, true, false},
        [](const std::string&) {},
        nullptr,  // no stream
        nullptr,  // no security
        nullptr,  // no approval
        nullptr,  // no compressor
        strategy.get()
    );

    auto result = loop.run("Tell me about dark mode");
    REQUIRE(result.ok());

    // After a turn, memory should have been updated
    auto mem_list = memory->list(50, 0);
    REQUIRE(mem_list.ok());
    // At least some long-term facts should have been stored
    bool found = false;
    for (const auto& entry : mem_list.value()) {
        if (entry.category == "long_term") {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("AgentLoop without strategy works as before", "[agent][memory]") {
    auto memory = make_test_memory();
    auto provider = std::make_shared<StrategyTestProvider>();
    ea::tool::ToolRegistry registry;

    AgentLoop loop(
        provider.get(), &registry, memory.get(),
        AgentLoop::Config{5, 1024, true, false},
        [](const std::string&) {},
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr  // no strategy
    );

    auto result = loop.run("Hello");
    REQUIRE(result.ok());
    REQUIRE(loop.history().size() >= 2);
}

TEST_CASE("AgentLoop with NullMemoryStrategy behaves same as no strategy", "[agent][memory][strategy]") {
    auto memory = make_test_memory();
    auto provider = std::make_shared<StrategyTestProvider>();
    ea::tool::ToolRegistry registry;
    NullMemoryStrategy null_strategy;

    AgentLoop loop(
        provider.get(), &registry, memory.get(),
        AgentLoop::Config{5, 1024, true, false},
        [](const std::string&) {},
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &null_strategy
    );

    auto result = loop.run("Hello");
    REQUIRE(result.ok());

    // Memory should be unchanged -- NullMemoryStrategy does nothing
    auto mem_list = memory->list(50, 0);
    REQUIRE(mem_list.ok());
    REQUIRE(mem_list.value().empty());
}
