// TuiApp — assembles all FTXUI components and drives the TUI event loop
#pragma once
#include "agent/AgentLoop.h"
#include "budget/BudgetTracker.h"
#include "conversation/IConversationStore.h"
#include "skill/Skill.h"
#include "ChatArea.h"
#include "InputBar.h"
#include "StatusBar.h"
#include "TopBar.h"
#include "Spinner.h"
#include "Banner.h"
#include "ApprovalDialog.h"
#include "CommandPalette.h"
#include "SkillsDialog.h"
#include "SessionsDialog.h"
#include "TuiEventListener.h"
#include "TuiApprovalHandler.h"
#include "memory/MemoryExtractor.h"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>

namespace ea::tui {

class TuiApp {
public:
    TuiApp();
    ~TuiApp();

    // Provide callbacks for AgentLoop construction — call BEFORE run()
    ea::agent::AgentLoop::OutputFn output_fn();
    ea::agent::AgentLoop::StreamFn stream_fn();
    ea::security::IApprovalHandler* approval_handler();

    void set_model(const std::string& model);
    void set_skills(ea::skill::SkillManager* skills) { skills_ = skills; }
    void set_plugins(ea::skill::PluginManager* plugins) { plugins_ = plugins; }
    // Background memory extraction worker (optional; null in tests/headless).
    void set_memory_extractor(ea::memory::MemoryExtractor* extractor) {
        memory_extractor_ = extractor;
    }

    // Global event routing used by the root CatchEvent (also testable).
    bool handle_global_event(ftxui::Event event);

    // Launch the TUI event loop (blocks until exit)
    void run(ea::agent::AgentLoop& loop,
             ea::budget::BudgetTracker* budget_tracker = nullptr,
             ea::conversation::IConversationStore* conv_store = nullptr);

    // Test accessors
    ChatArea& chat_area_for_test() { return chat_area_; }

private:
    ftxui::ScreenInteractive screen_{ftxui::ScreenInteractive::Fullscreen()};
    ChatArea chat_area_;
    InputBar input_bar_;
    StatusBar status_bar_;
    TopBar top_bar_{status_bar_.spinner_state()};
    TuiApprovalHandler approval_handler_;
    ApprovalDialog approval_dialog_{approval_handler_};
    CommandPalette command_palette_;
    SkillsDialog skills_dialog_;
    SessionsDialog sessions_dialog_;
    std::shared_ptr<TuiEventListener> event_listener_;
    std::thread heartbeat_thread_;
    std::atomic<bool> heartbeat_stop_{false};
    std::mutex pending_mutex_;
    struct PendingInput {
        std::string text;
        bool echoed;  // Already shown in chat (queued after turn output done)
    };
    std::deque<PendingInput> pending_inputs_;

    // AgentLoop thread management
    ea::agent::AgentLoop* loop_ = nullptr;
    ea::budget::BudgetTracker* budget_tracker_ = nullptr;
    ea::conversation::IConversationStore* conv_store_ = nullptr;
    ea::skill::SkillManager* skills_ = nullptr;
    ea::skill::PluginManager* plugins_ = nullptr;
    ea::memory::MemoryExtractor* memory_extractor_ = nullptr;
    std::thread agent_thread_;
    std::thread plugin_thread_;
    std::atomic<bool> agent_busy_{false};
    std::atomic<bool> plugin_busy_{false};
    // True when the current turn's visible output has finished (TurnEnd
    // fired) — the agent thread may still be doing post-turn memory work.
    std::atomic<bool> turn_output_done_{true};
    std::string model_;
    std::vector<CommandEntry> base_commands_;

    // Component tree
    ftxui::Component root_component_;
    ftxui::Component spinner_component_;
    bool approval_showing_ = false;
    bool palette_showing_ = false;
    bool skills_showing_ = false;
    bool sessions_showing_ = false;
    std::chrono::steady_clock::time_point last_redraw_request_{};

    void build_component_tree();
    void submit_input(const std::string& input);
    void execute_command(const std::string& command);
    void run_agent(const std::string& input);
    // Push the loop's current permission mode to all mode-aware UI parts.
    void sync_mode_ui();
    // Shift+Tab: default → acceptEdits → plan → default.
    void cycle_permission_mode();
    void run_plugin_async(std::string status,
                          std::function<Result<std::string>()> op,
                          const std::string& success_prefix,
                          const std::string& success_suffix = "");
    void rebuild_skill_commands();
    void start_new_session();
    void resume_session(const std::string& cid);
    void notify_session_end();
    void push_banner();
    // Coalesced redraw request; must only be called from the UI thread.
    void request_redraw();
};

}  // namespace ea::tui
