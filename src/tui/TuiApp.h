// TuiApp — assembles all FTXUI components and drives the TUI event loop
#pragma once
#include "agent/AgentLoop.h"
#include "budget/BudgetTracker.h"
#include "conversation/IConversationStore.h"
#include "ChatArea.h"
#include "InputBar.h"
#include "StatusBar.h"
#include "ApprovalDialog.h"
#include "CommandPalette.h"
#include "SessionSidebar.h"
#include "TuiEventListener.h"
#include "TuiApprovalHandler.h"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
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

    // Launch the TUI event loop (blocks until exit)
    void run(ea::agent::AgentLoop& loop,
             ea::budget::BudgetTracker* budget_tracker = nullptr,
             ea::conversation::IConversationStore* conv_store = nullptr);

private:
    ftxui::ScreenInteractive screen_{ftxui::ScreenInteractive::Fullscreen()};
    ChatArea chat_area_;
    InputBar input_bar_;
    StatusBar status_bar_;
    TuiApprovalHandler approval_handler_;
    ApprovalDialog approval_dialog_;
    CommandPalette command_palette_;
    SessionSidebar sidebar_;
    std::shared_ptr<TuiEventListener> event_listener_;

    // AgentLoop thread management
    ea::agent::AgentLoop* loop_ = nullptr;
    ea::budget::BudgetTracker* budget_tracker_ = nullptr;
    ea::conversation::IConversationStore* conv_store_ = nullptr;
    std::thread agent_thread_;
    std::atomic<bool> agent_busy_{false};

    // Component tree
    ftxui::Component root_component_;
    int sidebar_width_ = 0;  // 0 = hidden
    bool approval_showing_ = false;   // Modal state for approval dialog
    bool palette_showing_ = false;    // Modal state for command palette

    void build_component_tree();
    void submit_input(const std::string& input);
    void execute_command(const std::string& command);
    void run_agent(const std::string& input);
};

}  // namespace ea::tui
