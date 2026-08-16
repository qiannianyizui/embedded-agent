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
    // Extraction no longer happens inline per turn (background MemoryExtractor
    // owns it) — the turn must complete without any LLM extraction call.
    REQUIRE(provider->chat_count() == 1);
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
