#include "AgentLoop.h"
#include "SystemPrompt.h"
#include "platform/Platform.h"
#include "common/io/Logger.h"
#include "steps/HistoryPruneStep.h"
#include "steps/BuildToolSpecsStep.h"
#include "steps/CallProviderStep.h"
#include "steps/ParseResponseStep.h"
#include "steps/LoopDetectStep.h"
#include "steps/ExecuteToolsStep.h"
#include "steps/CollectResultsStep.h"

namespace ea::agent {

AgentLoop::AgentLoop(IProvider* provider,
                     ToolRegistry* registry,
                     IMemory* memory,
                     Config config,
                     OutputFn output)
    : provider_(provider)
    , registry_(registry)
    , memory_(memory)
    , config_(std::move(config))
    , output_(std::move(output))
{
    // Build default step chain
    steps_.push_back(std::make_unique<HistoryPruneStep>(config_.max_messages));
    steps_.push_back(std::make_unique<BuildToolSpecsStep>());
    steps_.push_back(std::make_unique<CallProviderStep>());
    steps_.push_back(std::make_unique<ParseResponseStep>());
    steps_.push_back(std::make_unique<ExecuteToolsStep>());
    steps_.push_back(std::make_unique<LoopDetectStep>(loop_detector_));
    steps_.push_back(std::make_unique<CollectResultsStep>());
}

Result<void> AgentLoop::run(const std::string& user_input) {
    interrupted_ = false;
    loop_detector_.reset();

    // Add user message to history
    history_.push_back({Role::User, user_input, std::nullopt, std::nullopt, std::nullopt});

    // Build system prompt once
    build_system_prompt_once();

    for (int i = 0; i < config_.max_iterations; ++i) {
        if (interrupted_) {
            EA_WARN("Agent loop interrupted at iteration {}", i);
            return Error::timeout("agent loop interrupted");
        }

        // Create turn context
        TurnContext ctx(history_, interrupted_);
        ctx.iteration = i;
        ctx.max_iterations = config_.max_iterations;
        ctx.max_tool_output_bytes = config_.max_tool_output_bytes;
        ctx.provider = provider_;
        ctx.registry = registry_;
        ctx.system_prompt = system_prompt_;

        // Run step chain
        auto result = run_step_chain(ctx, steps_);
        if (!result.ok()) {
            return result;
        }

        // Output final response if stopping
        if (ctx.should_stop && output_ && !ctx.response.content.empty()) {
            output_(ctx.response.content);
        }

        if (ctx.should_stop) {
            return {};
        }
    }

    // Max iterations reached
    EA_WARN("Max iterations ({}) reached", config_.max_iterations);
    if (output_) {
        output_("[Warning: Reached maximum iteration limit]");
    }
    return {};
}

void AgentLoop::build_system_prompt_once() {
    if (!system_prompt_.empty()) return;

    PromptContext ctx;
    ctx.soul = "You are a helpful AI assistant.";
    ctx.platform_info = platform::platform_description();

    // Get tool guidance
    auto specs = registry_->active_specs();
    std::string tool_guide = "Available tools:\n";
    for (const auto& spec : specs) {
        tool_guide += "- " + spec.name + ": " + spec.description + "\n";
    }
    ctx.tool_guidance = tool_guide;

    // Auto-inject relevant memories
    if (memory_ && config_.auto_memory) {
        for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
            if (it->role == Role::User) {
                EA_DEBUG("Querying memory with: {}", it->content);
                auto mem_result = memory_->recall(it->content, 5);
                if (mem_result.ok() && !mem_result.value().empty()) {
                    ctx.relevant_memories = std::move(mem_result.value());
                } else {
                    auto recent = memory_->list(5, 0);
                    if (recent.ok()) {
                        ctx.relevant_memories = std::move(recent.value());
                    }
                }
                break;
            }
        }
    }

    system_prompt_ = build_system_prompt(ctx);
}

void AgentLoop::interrupt() {
    interrupted_ = true;
}

const std::vector<Message>& AgentLoop::history() const {
    return history_;
}

void AgentLoop::clear_history() {
    history_.clear();
    system_prompt_.clear();
}

void AgentLoop::add_step(std::unique_ptr<ITurnStep> step) {
    steps_.push_back(std::move(step));
}

void AgentLoop::set_steps(std::vector<std::unique_ptr<ITurnStep>> steps) {
    steps_ = std::move(steps);
}

}  // namespace ea::agent
