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
#include "SessionSidebar.h"
#include "TuiEventListener.h"
#include "TuiApprovalHandler.h"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <chrono>
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

    // Launch the TUI event loop (blocks until exit)
    void run(ea::agent::AgentLoop& loop,
             ea::budget::BudgetTracker* budget_tracker = nullptr,
             ea::conversation::IConversationStore* conv_store = nullptr);

private:
    ftxui::ScreenInteractive screen_{ftxui::ScreenInteractive::Fullscreen()};
    ChatArea chat_area_;
    InputBar input_bar_;
    StatusBar status_bar_;
    TopBar top_bar_{status_bar_.spinner_state()};
    TuiApprovalHandler approval_handler_;
    ApprovalDialog approval_dialog_{approval_handler_};
    CommandPalette command_palette_;
    SessionSidebar sidebar_{nullptr};
    std::shared_ptr<TuiEventListener> event_listener_;
    std::thread heartbeat_thread_;
    std::atomic<bool> heartbeat_stop_{false};

    // AgentLoop thread management
    ea::agent::AgentLoop* loop_ = nullptr;
    ea::budget::BudgetTracker* budget_tracker_ = nullptr;
    ea::conversation::IConversationStore* conv_store_ = nullptr;
    ea::skill::SkillManager* skills_ = nullptr;
    std::thread agent_thread_;
    std::atomic<bool> agent_busy_{false};
    std::string model_;

    // Component tree
    ftxui::Component root_component_;
    ftxui::Component spinner_component_;
    int sidebar_width_ = 0;  // 0 = hidden
    bool approval_showing_ = false;
    bool palette_showing_ = false;
    std::chrono::steady_clock::time_point last_redraw_request_{};

    void build_component_tree();
    void submit_input(const std::string& input);
    void execute_command(const std::string& command);
    void run_agent(const std::string& input);
    void push_banner();
    // Coalesced redraw request; must only be called from the UI thread.
    void request_redraw();
};

}  // namespace ea::tui
