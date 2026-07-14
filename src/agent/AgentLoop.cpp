#include "AgentLoop.h"
#include "SystemPrompt.h"
#include "platform/Platform.h"
#include "common/io/Logger.h"

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
{}

Result<void> AgentLoop::run(const std::string& user_input) {
    interrupted_ = false;

    // Add user message to history
    history_.push_back({Role::User, user_input, std::nullopt, std::nullopt, std::nullopt});

    for (int i = 0; i < config_.max_iterations; ++i) {
        if (interrupted_) {
            EA_WARN("Agent loop interrupted at iteration {}", i);
            return Error::timeout("agent loop interrupted");
        }

        // Build messages with system prompt
        auto messages = build_messages();
        auto tool_specs = registry_->get_all_specs();

        // Call LLM
        ChatOptions opts;
        auto response = provider_->chat(messages, tool_specs, "", opts);
        if (!response.ok()) {
            EA_ERROR("LLM call failed: {}", response.error().message);
            return response.error();
        }

        auto& resp = response.value();

        // Add assistant message to history
        Message assistant_msg{Role::Assistant, resp.content, std::nullopt, std::nullopt, std::nullopt};
        if (!resp.tool_calls.empty()) {
            assistant_msg.tool_calls = resp.tool_calls;
        }
        history_.push_back(std::move(assistant_msg));

        // If no tool calls, we're done
        if (!resp.is_tool_use() || resp.tool_calls.empty()) {
            if (output_ && !resp.content.empty()) {
                output_(resp.content);
            }
            return {};
        }

        // Execute tool calls
        for (const auto& tc : resp.tool_calls) {
            EA_DEBUG("Executing tool: {} (id: {})", tc.name, tc.id);

            auto result = registry_->execute(tc.name, tc.arguments);
            ToolResult tool_result;
            if (result.ok()) {
                tool_result = std::move(result.value());
            } else {
                tool_result = ToolResult{tc.id, "Error: " + result.error().message, true};
            }

            // Truncate output if too long
            tool_result.output = truncate_output(tool_result.output);

            // Add tool result to history
            Message tool_msg{Role::Tool, tool_result.output, tc.name, std::nullopt, tc.id};
            history_.push_back(std::move(tool_msg));

            EA_DEBUG("Tool {} result: {} bytes, error={}", tc.name,
                     tool_result.output.size(), tool_result.is_error);
        }
    }

    // Max iterations reached
    EA_WARN("Max iterations ({}) reached", config_.max_iterations);
    if (output_) {
        output_("[Warning: Reached maximum iteration limit]");
    }
    return {};
}

std::vector<Message> AgentLoop::build_messages() const {
    std::vector<Message> messages;

    // Build system prompt (only on first call or when needed)
    if (system_prompt_.empty()) {
        PromptContext ctx;
        ctx.soul = "You are a helpful AI assistant.";
        ctx.platform_info = platform::platform_description();

        // Get tool guidance
        auto specs = registry_->get_all_specs();
        std::string tool_guide = "Available tools:\n";
        for (const auto& spec : specs) {
            tool_guide += "- " + spec.name + ": " + spec.description + "\n";
        }
        ctx.tool_guidance = tool_guide;

        // Auto-inject relevant memories
        if (memory_ && config_.auto_memory) {
            // Use the last user message as query for memory
            for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
                if (it->role == Role::User) {
                    EA_DEBUG("Querying memory with: {}", it->content);
                    auto mem_result = memory_->recall(it->content, 5);
                    if (mem_result.ok() && !mem_result.value().empty()) {
                        EA_DEBUG("Memory recall returned {} results", mem_result.value().size());
                        ctx.relevant_memories = std::move(mem_result.value());
                    } else {
                        // Fallback: fetch recent high-importance memories
                        EA_DEBUG("Memory recall returned 0, falling back to recent memories");
                        auto recent = memory_->list(5, 0);
                        if (recent.ok()) {
                            ctx.relevant_memories = std::move(recent.value());
                        }
                    }
                    break;
                }
            }
        }

        const_cast<std::string&>(system_prompt_) = build_system_prompt(ctx);
    }

    // System message
    messages.push_back({Role::System, system_prompt_, std::nullopt, std::nullopt, std::nullopt});

    // History
    for (const auto& msg : history_) {
        messages.push_back(msg);
    }

    return messages;
}

std::string AgentLoop::truncate_output(const std::string& output) const {
    if (static_cast<int>(output.size()) <= config_.max_tool_output_bytes) {
        return output;
    }
    return output.substr(0, config_.max_tool_output_bytes) + "\n... [truncated]";
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

}  // namespace ea::agent
