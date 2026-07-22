// TuiApp — assembles all FTXUI components and drives the TUI event loop
#include "TuiApp.h"
#include "common/io/Logger.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <sstream>
#include <iomanip>
#include <fstream>

namespace ea::tui {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

TuiApp::TuiApp()
    : approval_dialog_(approval_handler_)
    , sidebar_(nullptr) {  // conv_store set later in run()
}

TuiApp::~TuiApp() {
    if (agent_thread_.joinable()) {
        if (loop_) loop_->interrupt();
        agent_thread_.join();
    }
}

// ---------------------------------------------------------------------------
// Callbacks for AgentLoop
// ---------------------------------------------------------------------------

ea::agent::AgentLoop::OutputFn TuiApp::output_fn() {
    return [this](const std::string& text) {
        screen_.Post([this, text] {
            chat_area_.append_assistant(text);
        });
    };
}

ea::agent::AgentLoop::StreamFn TuiApp::stream_fn() {
    return [this](const ea::StreamChunk& chunk) {
        screen_.Post([this, chunk] {
            if (chunk.type == ea::StreamChunk::Type::Content) {
                chat_area_.append_stream_chunk(chunk);
            } else if (chunk.type == ea::StreamChunk::Type::Done) {
                chat_area_.finish_message();
            } else if (chunk.type == ea::StreamChunk::Type::Error) {
                chat_area_.append_error(chunk.data);
            }
            // ToolCallBegin / ToolCallDelta / ToolCallEnd are handled
            // by TuiEventListener via AgentEvent, not via StreamFn.
        });
    };
}

ea::security::IApprovalHandler* TuiApp::approval_handler() {
    return &approval_handler_;
}

// ---------------------------------------------------------------------------
// Component tree
// ---------------------------------------------------------------------------

void TuiApp::build_component_tree() {
    using namespace ftxui;

    // Main vertical layout: chat | separator | status | separator | input
    auto main_layout = Renderer([this] {
        return vbox({
            chat_area_.component()->Render() | flex | frame,
            separator(),
            status_bar_.component()->Render(),
            separator(),
            input_bar_.component()->Render(),
        });
    });

    // Optionally wrap with ResizableSplitLeft for sidebar
    // Always include the sidebar component; width 0 means hidden
    root_component_ = ResizableSplitLeft(sidebar_.component(), main_layout, &sidebar_width_);

    // Modal: approval dialog (Modal requires const bool*)
    approval_showing_ = approval_handler_.is_showing();
    root_component_ = Modal(root_component_, approval_dialog_.component(),
                            &approval_showing_);

    // Modal: command palette
    palette_showing_ = command_palette_.is_showing();
    root_component_ = Modal(root_component_, command_palette_.component(),
                            &palette_showing_);

    // Global key bindings — also syncs Modal bool pointers on each event
    root_component_ = CatchEvent(root_component_, [this](Event event) {
        // Sync Modal visibility state (read by Modal on each render)
        approval_showing_ = approval_handler_.is_showing();
        palette_showing_ = command_palette_.is_showing();

        // Ctrl+P: toggle command palette
        if (event == Event::CtrlP) {
            if (command_palette_.is_showing()) {
                command_palette_.hide();
            } else {
                command_palette_.show();
            }
            return true;
        }
        // Ctrl+S: toggle session sidebar
        if (event == Event::CtrlS) {
            sidebar_.toggle();
            sidebar_width_ = sidebar_.is_showing() ? 25 : 0;
            return true;
        }
        // Ctrl+C: interrupt agent or exit
        if (event == Event::CtrlC) {
            if (agent_busy_.load()) {
                if (loop_) loop_->interrupt();
                return true;
            }
            // Not busy — exit
            screen_.Exit();
            return true;
        }
        return false;
    });
}

// ---------------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------------

void TuiApp::submit_input(const std::string& input) {
    if (input.empty()) return;
    if (input[0] == '/') {
        execute_command(input);
        return;
    }
    chat_area_.append_user(input);
    run_agent(input);
}

void TuiApp::run_agent(const std::string& input) {
    if (!loop_) return;

    // Join any previous agent thread before starting a new one
    if (agent_thread_.joinable()) {
        agent_thread_.join();
    }

    agent_thread_ = std::thread([this, input] {
        agent_busy_.store(true);
        screen_.Post([this] {
            input_bar_.set_busy(true);
            status_bar_.set_busy(true);
        });

        auto result = loop_->run(input);

        // Update budget tracker session id after first run
        if (budget_tracker_ && !loop_->conversation_id().empty()) {
            budget_tracker_->set_session_id(loop_->conversation_id());
        }

        agent_busy_.store(false);
        screen_.Post([this] {
            input_bar_.set_busy(false);
            status_bar_.set_busy(false);
            status_bar_.set_session_id(loop_->conversation_id());
        });

        if (!result.ok()) {
            std::string err_msg = result.error().message;
            screen_.Post([this, err_msg] {
                chat_area_.append_error(err_msg);
            });
            EA_ERROR("Agent error: {}", err_msg);
        }
    });
}

void TuiApp::execute_command(const std::string& cmd) {
    if (cmd == "/quit" || cmd == "/exit") {
        screen_.Exit();
        return;
    }
    if (cmd == "/clear") {
        chat_area_.clear();
        return;
    }
    if (cmd == "/usage") {
        if (budget_tracker_) {
            auto su = budget_tracker_->session_usage();
            auto gu = budget_tracker_->global_usage();
            std::ostringstream oss;
            oss << "Session: " << su.total_tokens() << " tokens ("
                << su.input_tokens << " in / " << su.output_tokens << " out)\n"
                << "Global:  " << gu.total_tokens() << " tokens ("
                << gu.input_tokens << " in / " << gu.output_tokens << " out)";
            chat_area_.append_assistant(oss.str());
        } else {
            chat_area_.append_assistant("Budget tracking not enabled");
        }
        return;
    }
    if (cmd == "/cost") {
        if (budget_tracker_) {
            auto sc = budget_tracker_->session_cost();
            auto gc = budget_tracker_->global_cost();
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(4);
            oss << "Session: $" << sc.total() << "\n"
                << "Global:  $" << gc.total();
            chat_area_.append_assistant(oss.str());
        } else {
            chat_area_.append_assistant("Budget tracking not enabled");
        }
        return;
    }
    if (cmd == "/history") {
        if (conv_store_) {
            auto list = conv_store_->list(10, 0);
            if (list.ok() && !list.value().empty()) {
                std::ostringstream oss;
                for (const auto& m : list.value()) {
                    oss << "  " << m.id << "  " << m.title
                        << "  (" << m.message_count << " msgs)\n";
                }
                chat_area_.append_assistant(oss.str());
            } else if (list.ok()) {
                chat_area_.append_assistant("No conversations found");
            } else {
                chat_area_.append_error(list.error().message);
            }
        } else {
            chat_area_.append_assistant("Conversation persistence not available");
        }
        return;
    }
    if (cmd.substr(0, 8) == "/resume ") {
        if (conv_store_) {
            std::string cid = cmd.substr(8);
            // Remove trailing whitespace
            while (!cid.empty() && (cid.back() == ' ' || cid.back() == '\n'))
                cid.pop_back();
            auto msgs = conv_store_->load(cid);
            if (msgs.ok()) {
                loop_->restore_conversation(cid, std::move(msgs.value()));
                auto meta = conv_store_->get_meta(cid);
                std::string title = meta.ok() ? meta.value().title : cid;
                chat_area_.append_assistant("Resumed: " + title);
                status_bar_.set_session_id(cid);
            } else {
                chat_area_.append_error("Conversation not found: " + cid);
            }
        }
        return;
    }
    if (cmd == "/export") {
        if (conv_store_ && loop_ && !loop_->conversation_id().empty()) {
            auto data = conv_store_->export_jsonl(loop_->conversation_id());
            if (data.ok()) {
                chat_area_.append_assistant("Exported:\n" + data.value());
            } else {
                chat_area_.append_error("Export failed: " + data.error().message);
            }
        } else {
            chat_area_.append_assistant("No active conversation to export");
        }
        return;
    }
    if (cmd.substr(0, 8) == "/import ") {
        if (conv_store_) {
            std::string filepath = cmd.substr(8);
            while (!filepath.empty() && (filepath.back() == ' ' || filepath.back() == '\n'))
                filepath.pop_back();
            // Read file content
            std::ifstream file(filepath);
            if (!file.is_open()) {
                chat_area_.append_error("Cannot open file: " + filepath);
                return;
            }
            std::string jsonl_data((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
            auto new_id = conv_store_->import_jsonl(jsonl_data);
            if (new_id.ok()) {
                chat_area_.append_assistant("Imported conversation: " + new_id.value());
            } else {
                chat_area_.append_error("Import failed: " + new_id.error().message);
            }
        }
        return;
    }
    if (cmd == "/help") {
        chat_area_.append_assistant(
            "Commands:\n"
            "  /quit, /exit  — Exit the agent\n"
            "  /clear        — Clear chat history\n"
            "  /usage        — Show token usage\n"
            "  /cost         — Show cost\n"
            "  /history      — List conversations\n"
            "  /resume <id>  — Resume a conversation\n"
            "  /export       — Export current conversation as JSONL\n"
            "  /import <path>— Import conversation from JSONL file\n"
            "  /help         — Show this help\n"
            "\n"
            "Keybindings:\n"
            "  Ctrl+P  — Command palette\n"
            "  Ctrl+S  — Session sidebar\n"
            "  Ctrl+C  — Interrupt agent / exit"
        );
        return;
    }

    // Unknown command
    chat_area_.append_error("Unknown command: " + cmd + " (type /help for commands)");
}

// ---------------------------------------------------------------------------
// Main event loop
// ---------------------------------------------------------------------------

void TuiApp::run(ea::agent::AgentLoop& loop,
                 ea::budget::BudgetTracker* bt,
                 ea::conversation::IConversationStore* cs) {
    loop_ = &loop;
    budget_tracker_ = bt;
    conv_store_ = cs;

    // Update sidebar with conversation store
    sidebar_ = SessionSidebar(conv_store_);

    // Set up event listener — bridges AgentEvent to UI updates
    event_listener_ = std::make_shared<TuiEventListener>(
        chat_area_, status_bar_,
        [this](std::function<void()> fn) { screen_.Post(std::move(fn)); });
    loop.add_listener(event_listener_);

    // Set up input bar submit callback
    input_bar_.set_on_submit([this](const std::string& input) {
        submit_input(input);
    });

    // Set up command palette callback
    command_palette_.set_on_command([this](const std::string& cmd) {
        execute_command(cmd);
    });

    // Set up sidebar resume callback
    sidebar_.set_on_resume([this](std::string cid) {
        if (conv_store_) {
            auto msgs = conv_store_->load(cid);
            if (msgs.ok()) {
                loop_->restore_conversation(cid, std::move(msgs.value()));
                auto meta = conv_store_->get_meta(cid);
                std::string title = meta.ok() ? meta.value().title : cid;
                chat_area_.append_assistant("Resumed: " + title);
                status_bar_.set_session_id(cid);
            } else {
                chat_area_.append_error("Conversation not found: " + cid);
            }
        }
    });

    // Set model name in status bar
    status_bar_.set_model("agent");

    // Build the component tree
    build_component_tree();

    // Enter the FTXUI blocking event loop
    screen_.Loop(root_component_);

    // Cleanup: join agent thread if still running
    if (agent_thread_.joinable()) {
        if (loop_) loop_->interrupt();
        agent_thread_.join();
    }
}

}  // namespace ea::tui
