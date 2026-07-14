#pragma once
#include "core/IProvider.h"
#include "tool/ToolRegistry.h"
#include "core/IMemory.h"
#include "common/base/Result.h"
#include <string>
#include <vector>
#include <functional>
#include <atomic>

namespace ea::agent {

using ToolRegistry = tool::ToolRegistry;

class AgentLoop {
public:
    struct Config {
        int max_iterations = 90;
        int max_tool_output_bytes = 65536;
        bool auto_memory = true;
    };

    using OutputFn = std::function<void(const std::string&)>;

    AgentLoop(IProvider* provider,
              ToolRegistry* registry,
              IMemory* memory,
              Config config,
              OutputFn output);

    Result<void> run(const std::string& user_input);
    void interrupt();
    const std::vector<Message>& history() const;
    void clear_history();

private:
    std::vector<Message> build_messages() const;
    std::string truncate_output(const std::string& output) const;

    IProvider* provider_;
    ToolRegistry* registry_;
    IMemory* memory_;
    Config config_;
    OutputFn output_;

    std::vector<Message> history_;
    std::string system_prompt_;
    std::atomic<bool> interrupted_{false};
};

}  // namespace ea::agent
