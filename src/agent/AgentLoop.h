// AgentLoop — orchestrates ITurnStep chain for agent execution
#pragma once
#include "core/IProvider.h"
#include "tool/ToolRegistry.h"
#include "core/IMemory.h"
#include "common/base/Result.h"
#include "TurnContext.h"
#include "ITurnStep.h"
#include "IEventListener.h"
#include "LoopDetector.h"
#include "security/SecurityPolicy.h"
#include "security/IApprovalHandler.h"
#include "agent/ContextCompressor.h"
#include "IMemoryStrategy.h"
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <memory>

namespace ea::agent {

using ToolRegistry = tool::ToolRegistry;

class AgentLoop {
public:
    struct Config {
        int max_iterations = 90;
        int max_tool_output_bytes = 65536;
        int max_messages = 100;
        bool auto_memory = true;
        bool stream = true;  // Enable streaming output
    };

    using OutputFn = std::function<void(const std::string&)>;
    using StreamFn = std::function<void(const StreamChunk&)>;

    AgentLoop(IProvider* provider,
              ToolRegistry* registry,
              IMemory* memory,
              Config config,
              OutputFn output,
              StreamFn stream_fn = nullptr,
              security::SecurityPolicy* policy = nullptr,
              security::IApprovalHandler* approval = nullptr,
              ContextCompressor* compressor = nullptr,
              IMemoryStrategy* strategy = nullptr);

    Result<void> run(const std::string& user_input);
    void interrupt();
    const std::vector<Message>& history() const;
    void clear_history();

    // Step chain customization
    void add_step(std::unique_ptr<ITurnStep> step);
    void set_steps(std::vector<std::unique_ptr<ITurnStep>> steps);

    // Event listener management
    void add_listener(std::shared_ptr<IEventListener> listener);
    void remove_listener(const std::shared_ptr<IEventListener>& listener);

private:
    void build_system_prompt_once();
    void emit(AgentEventType type, const TurnContext& ctx);
    void emit_event(const AgentEvent& event);

    IProvider* provider_;
    ToolRegistry* registry_;
    IMemory* memory_;
    Config config_;
    OutputFn output_;
    StreamFn stream_fn_;
    security::SecurityPolicy* policy_;
    security::IApprovalHandler* approval_;
    ContextCompressor* compressor_;
    IMemoryStrategy* strategy_;

    std::vector<Message> history_;
    std::string base_system_prompt_;
    std::string system_prompt_;
    std::atomic<bool> interrupted_{false};

    // Step chain
    std::vector<std::unique_ptr<ITurnStep>> steps_;
    LoopDetector loop_detector_;
    std::vector<std::shared_ptr<IEventListener>> listeners_;
};

}  // namespace ea::agent
