#pragma once
#include "agent/AgentLoop.h"
#include "provider/ProviderFactory.h"
#include "memory/SqliteMemory.h"
#include "tool/ToolRegistry.h"
#include "config/Config.h"
#include "Version.h"

namespace ea {

class Agent {
public:
    explicit Agent(const std::string& config_path = "") {
        auto cfg = config::load(config_path);
        if (!cfg.ok()) return;

        config_ = cfg.value();
        provider_ = provider::create(config_.provider);
        memory_ = std::make_unique<memory::SqliteMemory>(
            memory::SqliteMemory::Config{config_.memory.path, config_.memory.enable_fts5});

        registry_ = std::make_unique<tool::ToolRegistry>();
        // Tools will be registered by the caller
    }

    std::string chat(const std::string& input) {
        if (!provider_ || !memory_ || !registry_) return "Agent not initialized";

        std::string output;
        agent::AgentLoop loop(provider_.get(), registry_.get(), memory_.get(),
            agent::AgentLoop::Config{config_.agent.max_iterations},
            [&](const std::string& t) { output = t; });

        auto result = loop.run(input);
        if (!result.ok()) return "Error: " + result.error().message;
        return output;
    }

    config::AppConfig config_;
    std::unique_ptr<IProvider> provider_;
    std::unique_ptr<memory::SqliteMemory> memory_;
    std::unique_ptr<tool::ToolRegistry> registry_;
};

}  // namespace ea
