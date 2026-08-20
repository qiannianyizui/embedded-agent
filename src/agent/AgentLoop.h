// AgentLoop — orchestrates ITurnStep chain for agent execution
#pragma once
#include "provider/IProvider.h"
#include "tool/ToolRegistry.h"
#include "memory/IMemory.h"
#include "base/Result.h"
#include "TurnContext.h"
#include "ITurnStep.h"
#include "IEventListener.h"
#include "LoopDetector.h"
#include "security/SecurityPolicy.h"
#include "security/IApprovalHandler.h"
#include "agent/ContextCompressor.h"
#include "IMemoryStrategy.h"
#include "conversation/IConversationStore.h"
#include "budget/BudgetTracker.h"
#include "memory/CuratedMemoryStore.h"
#include "agent/PermissionMode.h"
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
        bool auto_memory = true;
        bool stream = true;  // Enable streaming output
        bool auto_persist = true;
        std::string model;  // Model name for trace attribution
        std::string soul;   // Identity text (from SOUL.md or config)
        std::string context_files;  // Project context file content
        std::string skills_index;   // Rendered <available_skills> block
        std::string onboarding_directive;  // First-run profile-build directive
        std::shared_ptr<PermissionState> permission;  // nullptr = modes disabled
        std::string plan_dir;  // override for plan file directory (default: worktree/.opencode/plans)
        int max_tokens = 0;  // 0 = don't send max_tokens (model default)
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
              IMemoryStrategy* strategy = nullptr,
              conversation::IConversationStore* conv_store = nullptr,
              budget::BudgetTracker* budget_tracker = nullptr,
              memory::CuratedMemoryStore* curated_memory = nullptr);

    Result<void> run(const std::string& user_input);
    void interrupt();
    void set_onboarding_directive(std::string directive) {
        config_.onboarding_directive = std::move(directive);
        onboarding_injected_ = false;
    }
    const std::vector<Message>& history() const;
    void clear_history();

    // Conversation persistence
    void restore_conversation(const std::string& conversation_id,
                              std::vector<Message> messages);
    const std::string& conversation_id() const { return conversation_id_; }

    // Permission modes (Claude Code style)
    PermissionMode permission_mode() const;
    bool plan_mode() const;  // convenience: permission_mode() == Plan
    const std::string& plan_file() const;
    Result<void> set_permission_mode(PermissionMode mode);
    Result<void> set_plan_mode(bool active);

    struct CompressionResult {
        int before = 0;
        int after = 0;
        bool compressed = false;
    };
    // Manual /compress [focus] — Hermes-style live context compaction.
    Result<CompressionResult> compress_context(const std::string& focus_topic = {});

    // Budget tracking
    budget::BudgetTracker* budget_tracker() const { return budget_tracker_; }

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
    void append_to_history(Message msg);
    void persist_new_messages();
    void persist_or_archive(bool compression_applied);

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
    conversation::IConversationStore* conv_store_;
    budget::BudgetTracker* budget_tracker_;
    memory::CuratedMemoryStore* curated_memory_ = nullptr;

    std::vector<Message> history_;
    std::string base_system_prompt_;
    std::string system_prompt_;
    std::string conversation_id_;
    std::string plan_file_;
    std::vector<Message> pending_persist_;
    std::atomic<bool> interrupted_{false};
    bool onboarding_injected_ = false;

    // Step chain
    std::vector<std::unique_ptr<ITurnStep>> steps_;
    LoopDetector loop_detector_;
    std::vector<std::shared_ptr<IEventListener>> listeners_;
    std::string current_trace_id_;   // trace_id for the current run()

    Result<std::string> resolve_plan_file();
    void emit_mode_changed(const std::string& mode);
};

}  // namespace ea::agent
