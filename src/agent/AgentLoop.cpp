#include "AgentLoop.h"
#include "AgentEvent.h"
#include "SystemPrompt.h"
#include "platform/Platform.h"
#include "log/Logger.h"
#include "steps/BuildToolSpecsStep.h"
#include "steps/CallProviderStep.h"
#include "steps/ParseResponseStep.h"
#include "steps/LoopDetectStep.h"
#include "steps/ExecuteToolsStep.h"
#include "steps/CollectResultsStep.h"
#include "trace/TraceEvent.h"
#include "io/FileSystem.h"
#include <algorithm>
#include <ctime>
#include <unistd.h>

namespace ea::agent {

namespace {

// Older versions persisted the first-run onboarding directive as a suffix of
// the first user message. Strip it on restore so it is never shown as user
// input or fed back to the model after a restart.
void strip_legacy_onboarding_directive(std::vector<Message>& messages) {
    const std::string marker =
        "[System note: This is the user's very first message ever.";
    for (auto& msg : messages) {
        if (msg.role != Role::User) continue;
        auto pos = msg.content.find(marker);
        if (pos == std::string::npos) continue;
        size_t start = pos;
        if (start >= 2 && msg.content.compare(start - 2, 2, "\n\n") == 0) {
            start -= 2;
        }
        msg.content.erase(start);
    }
}

const char* PLAN_MODE_PROMPT = R"(
## PLAN MODE — READ-ONLY

You are in PLAN MODE. You MUST NOT modify any file except the plan file, run
shell commands, delegate work to sub-agents, or otherwise change system state.
This constraint overrides any other instruction you receive.

Plan file: ${plan_file}

Workflow:
1. Explore the codebase with read-only tools (file read, search, web).
2. Ask the user clarifying questions when needed.
3. Write the final plan to the plan file with the file tool. Include the files
   that will change and a verification section.
4. Call plan_exit only after the plan file is complete.
)";

}  // namespace

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
                     conversation::IConversationStore* conv_store,
                     budget::BudgetTracker* budget_tracker,
                     memory::CuratedMemoryStore* curated_memory)
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
    , budget_tracker_(budget_tracker)
    , curated_memory_(curated_memory)
{
    // Build default step chain
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
    current_trace_id_ = trace::generate_uuid();
    bool compression_applied = false;
    const PermissionMode mode = config_.permission
        ? config_.permission->mode.load() : PermissionMode::Manual;
    const bool plan_active = mode == PermissionMode::Plan;

    // Inject the first-run onboarding directive into the system prompt only.
    // It must never be persisted into history, otherwise restarting the app
    // would render an injected system note as if it were the user's message.
    bool inject_onboarding =
        !config_.onboarding_directive.empty() && !onboarding_injected_ &&
        history_.empty();
    if (inject_onboarding) {
        onboarding_injected_ = true;
    }

    Message user_msg{Role::User, user_input,
                     std::nullopt, std::nullopt, std::nullopt};
    user_msg.mode = permission_mode_name(mode);
    if (plan_active) user_msg.plan_file = plan_file_;
    append_to_history(std::move(user_msg));

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
        ctx.max_tokens = config_.max_tokens;
        ctx.provider = provider_;
        ctx.registry = registry_;
        ctx.system_prompt = system_prompt_;
        if (inject_onboarding) {
            ctx.system_prompt += "\n" + config_.onboarding_directive;
        }
        ctx.model = config_.model;
        ctx.permission_mode = config_.permission
            ? config_.permission->mode.load() : PermissionMode::Manual;
        ctx.plan_file = plan_file_;
        ctx.stream_callback = stream_fn_;
        ctx.emit_fn = [this](const AgentEvent& e) { emit_event(e); };
        ctx.append_message_fn = [this](Message msg) {
            append_to_history(std::move(msg));
        };
        ctx.budget_tracker = budget_tracker_;

        // Emit TurnStart
        emit(AgentEventType::TurnStart, ctx);

        // Run step chain
        auto result = run_step_chain(ctx, steps_);
        if (!result.ok()) {
            compression_applied = compression_applied || ctx.compression_applied;
            AgentEvent err_event;
            err_event.type = AgentEventType::Error;
            err_event.iteration = ctx.iteration;
            err_event.agent_id = ctx.agent_id;
            err_event.error_message = result.error().message;
            err_event.trace_id = current_trace_id_;
            emit_event(err_event);
            return result;
        }
        compression_applied = compression_applied || ctx.compression_applied;

        // Plan approved via plan_exit: switch to the non-plan mode saved when
        // planning started and continue with a synthetic user message so the
        // same run implements the plan.
        if (config_.permission && config_.permission->exit_approved.load()) {
            config_.permission->exit_approved.store(false);
            PermissionMode restored = config_.permission->previous.load();
            if (restored == PermissionMode::Plan) restored = PermissionMode::Manual;
            config_.permission->mode.store(restored);
            Message plan_msg{Role::User,
                             "Plan approved. Execute the plan at " + plan_file_,
                             std::nullopt, std::nullopt, std::nullopt};
            plan_msg.mode = permission_mode_name(restored);
            plan_msg.plan_file = plan_file_;
            append_to_history(std::move(plan_msg));
            loop_detector_.reset();
            base_system_prompt_.clear();
            build_system_prompt_once();
            if (strategy_ && memory_) {
                auto mem_prompt = strategy_->build_memory_prompt(history_, memory_);
                system_prompt_ = base_system_prompt_;
                if (!mem_prompt.empty()) {
                    system_prompt_ += "\n" + mem_prompt;
                }
            }
            emit_mode_changed(permission_mode_name(restored));
            continue;
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

            persist_or_archive(compression_applied);

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
    persist_or_archive(compression_applied);
    if (output_) {
        output_("[Warning: Reached maximum iteration limit]");
    }
    return {};
}

void AgentLoop::build_system_prompt_once() {
    if (!base_system_prompt_.empty()) return;

    PromptContext ctx;

    // Stable layer: soul (config overrides default)
    ctx.soul = config_.soul.empty()
        ? "You are a helpful AI assistant."
        : config_.soul;
    ctx.platform_info = platform::platform_description();

    // Get tool guidance
    auto specs = registry_->active_specs();
    std::string tool_guide = "Available tools:\n";
    for (const auto& spec : specs) {
        tool_guide += "- " + spec.name + ": " + spec.description + "\n";
    }
    ctx.tool_guidance = tool_guide;

    // Context layer: project context files
    ctx.context_files = config_.context_files;
    ctx.skills_index = config_.skills_index;
    if (curated_memory_) {
        ctx.curated_memory = curated_memory_->format_for_system_prompt(
            memory::MemoryTarget::Memory);
        ctx.user_profile = curated_memory_->format_for_system_prompt(
            memory::MemoryTarget::User);
    }

    // Volatile layer: memories
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

    if (config_.permission &&
        config_.permission->mode.load() == PermissionMode::Plan) {
        std::string plan_prompt = PLAN_MODE_PROMPT;
        const std::string marker = "${plan_file}";
        auto pos = plan_prompt.find(marker);
        if (pos != std::string::npos) {
            plan_prompt.replace(pos, marker.size(), plan_file_);
        }
        base_system_prompt_ += "\n\n" + plan_prompt;
        system_prompt_ = base_system_prompt_;
    }
}

void AgentLoop::interrupt() {
    interrupted_ = true;
}

PermissionMode AgentLoop::permission_mode() const {
    return config_.permission ? config_.permission->mode.load()
                              : PermissionMode::Manual;
}

bool AgentLoop::plan_mode() const {
    return permission_mode() == PermissionMode::Plan;
}

const std::string& AgentLoop::plan_file() const {
    return plan_file_;
}

Result<void> AgentLoop::set_permission_mode(PermissionMode m) {
    if (!config_.permission) {
        return Error::invalid_arg("permission modes are not available");
    }
    if (config_.permission->mode.load() == m) {
        return {};
    }
    if (m == PermissionMode::Plan) {
        return set_plan_mode(true);
    }
    if (config_.permission->mode.load() == PermissionMode::Plan) {
        // Leaving plan mode — clear the pending plan_exit handoff.
        config_.permission->exit_approved.store(false);
    }
    config_.permission->mode.store(m);
    // Remember the last non-plan mode so leaving plan mode (Shift+Tab)
    // restores it.
    config_.permission->previous.store(m);
    base_system_prompt_.clear();
    system_prompt_.clear();
    return {};
}

Result<void> AgentLoop::set_plan_mode(bool active) {
    if (!config_.permission) {
        return Error::invalid_arg("plan mode is not available");
    }
    const PermissionMode cur = config_.permission->mode.load();
    if (active == (cur == PermissionMode::Plan)) {
        return {};
    }

    if (active) {
        auto path = resolve_plan_file();
        if (!path.ok()) return path.error();
        plan_file_ = path.value();
        config_.permission->plan_file = plan_file_;

        auto dir = fs::mkdir_p(fs::parent_path(plan_file_));
        if (!dir.ok()) {
            plan_file_.clear();
            config_.permission->plan_file.clear();
            return dir.error();
        }
        config_.permission->mode.store(PermissionMode::Plan);
    } else {
        config_.permission->exit_approved.store(false);
        PermissionMode restored = config_.permission->previous.load();
        if (restored == PermissionMode::Plan) restored = PermissionMode::Manual;
        config_.permission->mode.store(restored);
    }

    base_system_prompt_.clear();
    system_prompt_.clear();
    return {};
}

Result<std::string> AgentLoop::resolve_plan_file() {
    std::string base = config_.plan_dir;
    if (base.empty()) {
        char buf[4096];
        std::string cwd = getcwd(buf, sizeof(buf)) ? buf : ".";
        auto git = fs::find_git_root(cwd);
        if (git.ok()) {
            base = git.value() + "/.opencode/plans";
        } else {
            auto data = fs::data_dir();
            base = data.ok() ? data.value() + "/plans" : cwd + "/.opencode/plans";
        }
    }

    std::string slug = conversation_id_.empty() ? "draft" : conversation_id_;
    return base + "/plan-" + std::to_string(std::time(nullptr)) + "-" + slug + ".md";
}

void AgentLoop::emit_mode_changed(const std::string& mode) {
    AgentEvent event;
    event.type = AgentEventType::ModeChanged;
    event.mode = mode;
    event.trace_id = current_trace_id_;
    emit_event(event);
}

const std::vector<Message>& AgentLoop::history() const {
    return history_;
}

void AgentLoop::clear_history() {
    history_.clear();
    base_system_prompt_.clear();
    system_prompt_.clear();
    conversation_id_.clear();
    pending_persist_.clear();
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
    event.trace_id = current_trace_id_;

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
        if (ctx.budget_tracker) {
            event.turn_usage = ctx.budget_tracker->session_usage();
            event.turn_cost = ctx.budget_tracker->session_cost();
        }
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

    for (const auto& msg : pending_persist_) {
        auto r = conv_store_->append(conversation_id_, msg);
        if (!r.ok()) {
            EA_WARN("Failed to persist message: {}", r.error().message);
            // Don't block — just log and continue
        }
    }
    pending_persist_.clear();
}

void AgentLoop::persist_or_archive(bool compression_applied) {
    if (compression_applied && conv_store_ && config_.auto_persist &&
        !conversation_id_.empty() && compressor_ && compressor_->config().in_place) {
        // Persist the turn's original messages first so the full transcript
        // survives the compaction as archived rows.
        persist_new_messages();
        auto r = conv_store_->archive_and_compact(conversation_id_, history_);
        if (r.ok()) {
            pending_persist_.clear();
        } else {
            EA_WARN("Archive-and-compact failed: {}", r.error().message);
        }
        return;
    }
    persist_new_messages();
}

void AgentLoop::restore_conversation(const std::string& conversation_id,
                                      std::vector<Message> messages) {
    conversation_id_ = conversation_id;
    strip_legacy_onboarding_directive(messages);
    history_ = std::move(messages);
    pending_persist_.clear();
    base_system_prompt_.clear();  // Force rebuild on next run()

    if (!config_.permission) return;
    config_.permission->exit_approved.store(false);
    for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
        if (it->role != Role::User) continue;
        const PermissionMode pm = permission_mode_from_name(it->mode);
        config_.permission->mode.store(pm);
        if (pm != PermissionMode::Plan) {
            config_.permission->previous.store(pm);
        }
        if (pm == PermissionMode::Plan) {
            if (!it->plan_file.empty()) {
                plan_file_ = it->plan_file;
                config_.permission->plan_file = plan_file_;
            } else if (auto path = resolve_plan_file(); path.ok()) {
                plan_file_ = path.value();
                config_.permission->plan_file = plan_file_;
                auto dir = fs::mkdir_p(fs::parent_path(plan_file_));
                if (!dir.ok()) {
                    EA_WARN("Failed to create plan dir: {}", dir.error().message);
                }
            }
        } else {
            plan_file_.clear();
            config_.permission->plan_file.clear();
        }
        break;
    }
}

void AgentLoop::append_to_history(Message msg) {
    // Keep durable conversation messages separate from context-only system
    // markers (breadcrumbs, loop warnings) so pruning can never lose them.
    if (msg.role != Role::System) {
        pending_persist_.push_back(msg);
    }
    history_.push_back(std::move(msg));
}

Result<AgentLoop::CompressionResult> AgentLoop::compress_context(
        const std::string& focus_topic) {
    if (!compressor_) {
        return Error::invalid_arg("Context compression is not enabled");
    }

    std::vector<Message> messages = history_;
    if (!base_system_prompt_.empty()) {
        messages.insert(messages.begin(),
                        {Role::System, base_system_prompt_,
                         std::nullopt, std::nullopt, std::nullopt});
    }

    auto result = compressor_->compress(messages, focus_topic);
    if (!result.ok()) return result.error();

    CompressionResult cr;
    cr.before = static_cast<int>(history_.size());
    cr.after = cr.before;
    if (!compressor_->compressed_last_call()) return cr;

    std::vector<Message> live;
    const auto& compressed = result.value();
    if (!compressed.empty() && compressed[0].role == Role::System) {
        live.assign(compressed.begin() + 1, compressed.end());
    } else {
        live = compressed;
    }
    cr.after = static_cast<int>(live.size());
    cr.compressed = true;

    if (conv_store_ && config_.auto_persist && !conversation_id_.empty() &&
        compressor_->config().in_place) {
        persist_new_messages();
        auto ar = conv_store_->archive_and_compact(conversation_id_, live);
        if (!ar.ok()) {
            EA_WARN("Archive-and-compact failed: {}", ar.error().message);
            return ar.error();
        }
    }

    history_ = std::move(live);
    pending_persist_.clear();
    return cr;
}

}  // namespace ea::agent
