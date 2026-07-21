// tests/common/Fixtures.h
#pragma once
#include "MockProvider.h"
#include "MockTool.h"
#include "TestHelpers.h"
#include "memory/InMemoryBackend.h"
#include "memory/SqliteMemory.h"
#include "memory/MemoryManager.h"
#include "tool/ToolRegistry.h"
#include "agent/AgentLoop.h"

namespace ea::test {

struct MemoryFixture {
    std::unique_ptr<memory::InMemoryBackend> in_memory;

    MemoryFixture() {
        in_memory = std::make_unique<memory::InMemoryBackend>();
    }
    ~MemoryFixture() = default;
};

struct AgentLoopFixture {
    MockProvider provider;
    tool::ToolRegistry registry;
    std::unique_ptr<memory::MemoryManager> memory;
    std::string last_output;
    std::unique_ptr<agent::AgentLoop> loop;

    AgentLoopFixture() {
        auto backend = std::make_unique<memory::InMemoryBackend>();
        memory = std::make_unique<memory::MemoryManager>(std::move(backend));
        loop = std::make_unique<agent::AgentLoop>(
            &provider, &registry, nullptr,
            agent::AgentLoop::Config{},
            [this](const std::string& t) { last_output = t; }
        );
    }

    void run(std::string input) {
        auto result = loop->run(std::move(input));
        // Store result for inspection if needed
    }
};

}  // namespace ea::test
