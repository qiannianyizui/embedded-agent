#include "AgentLoop.h"
#include "AgentEvent.h"
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
#include <algorithm>

namespace ea::agent {

AgentLoop::AgentLoop(IProvider* provider,
                     ToolRegistry* registry,
                     IMemory* memory,
                     Config config,
                     OutputFn output,
                     StreamFn stream_fn,
                     security::SecurityPolicy* policy,
                     security::IApprovalHandler* approval,
                     ContextCompressor* compressor,
                     IMemoryStrategy* strategy,
                     conversation::IConversationStore* conv_store)
    : provider_(provider)
    , registry_(registry)
    , memory_(memory)
    , config_(std::move(config))
    , output_(std::move(output))
    , stream_fn_(std::move(stream_fn))
    , policy_(policy)
    , approval_(approval)
    , compressor_(compressor)
    , strategy_(strategy)
    , conv_store_(conv_store)
{
    // Build default step chain
    steps_.push_back(std::make_unique<HistoryPruneStep>(config_.max_messages));
    steps_.push_back(std::make_unique<BuildToolSpecsStep>());
    steps_.push_back(std::make_unique<CallProviderStep>(compressor_));
    steps_.push_back(std::make_unique<ParseResponseStep>());
    steps_.push_back(std::make_unique<ExecuteToolsStep>(policy_, approval_));
    steps_.push_back(std::make_unique<LoopDetectStep>(loop_detector_));
    steps_.push_back(std::make_unique<CollectResultsStep>());
}

Result<void> AgentLoop::run(const std::string& user_input) {
    interrupted_ = false;
    loop_detector_.reset();

    // Add user message to history
    history_.push_back({Role::User, user_input, std::nullopt, std::nullopt, std::nullopt});

    // Build system prompt once (base part)
    build_system_prompt_once();

    // Refresh strategy prompt each turn so new facts/summaries appear
    if (strategy_ && memory_) {
        auto mem_prompt = strategy_->build_memory_prompt(history_, memory_);
        system_prompt_ = base_system_prompt_;
        if (!mem_prompt.empty()) {
            system_prompt_ += "\n" + mem_prompt;
        }
    }

    // Create conversation on first run
    if (conv_store_ && config_.auto_persist && conversation_id_.empty()) {
        auto r = conv_store_->create();
        if (r.ok()) {
            conversation_id_ = r.value();
            saved_count_ = 0;
        } else {
            EA_WARN("Failed to create conversation: {}", r.error().message);
        }
    }

    for (int i = 0; i < config_.max_iterations; ++i) {
        if (interrupted_) {
            EA_WARN("Agent loop interrupted at iteration {}", i);
            emit(AgentEventType::Interrupt, TurnContext(history_, interrupted_));
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
        ctx.stream_callback = stream_fn_;
        ctx.emit_fn = [this](const AgentEvent& e) { emit_event(e); };

        // Emit TurnStart
        emit(AgentEventType::TurnStart, ctx);

        // Run step chain
        auto result = run_step_chain(ctx, steps_);
        if (!result.ok()) {
            AgentEvent err_event;
            err_event.type = AgentEventType::Error;
            err_event.iteration = ctx.iteration;
            err_event.agent_id = ctx.agent_id;
            err_event.error_message = result.error().message;
            emit_event(err_event);
            return result;
        }

        // Output final response if stopping
        // In streaming mode, content is already delivered via StreamFn — skip OutputFn
        if (ctx.should_stop) {
            emit(AgentEventType::TurnEnd, ctx);
            if (!config_.stream || !stream_fn_) {
                if (output_ && !ctx.response.content.empty()) {
                    output_(ctx.response.content);
                }
            }

            // After successful turn, let memory strategy process this turn
            if (strategy_ && memory_ && provider_) {
                // Find last assistant output
                std::string last_output;
                for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
                    if (it->role == Role::Assistant) {
                        last_output = it->content;
                        break;
                    }
                }
                MemoryStrategyContext mctx{history_, memory_, provider_, user_input, last_output};
                strategy_->on_turn_end(mctx);
            }

            persist_new_messages();

            return {};
        }
    }

    // Max iterations reached — still call on_turn_end so strategy can process
    EA_WARN("Max iterations ({}) reached", config_.max_iterations);
    if (strategy_ && memory_ && provider_) {
        std::string last_output;
        for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
            if (it->role == Role::Assistant) {
                last_output = it->content;
                break;
            }
        }
        MemoryStrategyContext mctx{history_, memory_, provider_, user_input, last_output};
        strategy_->on_turn_end(mctx);
    }
    persist_new_messages();
    if (output_) {
        output_("[Warning: Reached maximum iteration limit]");
    }
    return {};
}

void AgentLoop::build_system_prompt_once() {
    if (!base_system_prompt_.empty()) return;

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

    base_system_prompt_ = build_system_prompt(ctx);
    system_prompt_ = base_system_prompt_;
}

void AgentLoop::interrupt() {
    interrupted_ = true;
}

const std::vector<Message>& AgentLoop::history() const {
    return history_;
}

void AgentLoop::clear_history() {
    history_.clear();
    base_system_prompt_.clear();
    system_prompt_.clear();
    conversation_id_.clear();
    saved_count_ = 0;
}

void AgentLoop::add_step(std::unique_ptr<ITurnStep> step) {
    steps_.push_back(std::move(step));
}

void AgentLoop::set_steps(std::vector<std::unique_ptr<ITurnStep>> steps) {
    steps_ = std::move(steps);
}

void AgentLoop::add_listener(std::shared_ptr<IEventListener> listener) {
    listeners_.push_back(std::move(listener));
}

void AgentLoop::remove_listener(const std::shared_ptr<IEventListener>& listener) {
    auto it = std::find(listeners_.begin(), listeners_.end(), listener);
    if (it != listeners_.end()) {
        listeners_.erase(it);
    }
}

void AgentLoop::emit(AgentEventType type, const TurnContext& ctx) {
    if (listeners_.empty()) return;

    AgentEvent event;
    event.type = type;
    event.iteration = ctx.iteration;
    event.agent_id = ctx.agent_id;

    switch (type) {
    case AgentEventType::TurnStart:
        for (auto it = ctx.messages.rbegin(); it != ctx.messages.rend(); ++it) {
            if (it->role == Role::User) {
                event.user_input = it->content;
                break;
            }
        }
        break;
    case AgentEventType::TurnEnd:
        event.assistant_output = ctx.response.content;
        break;
    case AgentEventType::LLMResponse:
        event.assistant_output = ctx.response.content;
        event.usage = ctx.response.usage;
        break;
    case AgentEventType::Interrupt:
        break;
    default:
        break;
    }

    emit_event(event);
}

void AgentLoop::emit_event(const AgentEvent& event) {
    for (const auto& listener : listeners_) {
        listener->on_event(event);
    }
}

void AgentLoop::persist_new_messages() {
    if (!conv_store_ || !config_.auto_persist || conversation_id_.empty()) return;

    for (size_t i = saved_count_; i < history_.size(); ++i) {
        auto r = conv_store_->append(conversation_id_, history_[i]);
        if (!r.ok()) {
            EA_WARN("Failed to persist message: {}", r.error().message);
            // Don't block — just log and continue
        }
    }
    saved_count_ = history_.size();
}

void AgentLoop::restore_conversation(const std::string& conversation_id,
                                      std::vector<Message> messages) {
    conversation_id_ = conversation_id;
    history_ = std::move(messages);
    saved_count_ = history_.size();
    base_system_prompt_.clear();  // Force rebuild on next run()
}

}  // namespace ea::agent
