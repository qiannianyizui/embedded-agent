// TuiApp — assembles all FTXUI components and drives the TUI event loop
#include "TuiApp.h"
#include "log/Logger.h"
#include "trace/Trace.h"
#include "Theme.h"
#include "FormatUtils.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/animation.hpp>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <unistd.h>  // getcwd

namespace ea::tui {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

TuiApp::TuiApp() = default;

TuiApp::~TuiApp() {
    heartbeat_stop_.store(true);
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
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
            request_redraw();
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
            request_redraw();
        });
    };
}

ea::security::IApprovalHandler* TuiApp::approval_handler() {
    return &approval_handler_;
}

void TuiApp::set_model(const std::string& model) {
    model_ = model;
}

// ---------------------------------------------------------------------------
// Banner
// ---------------------------------------------------------------------------

void TuiApp::push_banner() {
    auto& theme = default_theme();
    BannerInfo info;
    info.model = model_;
    info.session_id = loop_ ? loop_->conversation_id() : "";

    char cwd_buf[4096];
    if (getcwd(cwd_buf, sizeof(cwd_buf))) {
        info.cwd = cwd_buf;
    }

    std::ostringstream oss;
    oss << theme.brand.icon << " " << theme.brand.name << "\n";
    if (!info.model.empty()) {
        oss << "  model      " << info.model << "\n";
    }
    if (!info.cwd.empty()) {
        oss << "  workspace  " << info.cwd << "\n";
    }
    oss << "  session    " << (info.session_id.empty()
                                   ? "(new)"
                                   : shortId(info.session_id)) << "\n";
    if (info.tool_count > 0) {
        oss << "  tools      " << info.tool_count << " available\n";
    }
    oss << "\n" << theme.brand.welcome << "\n"
        << "\n  Ctrl+P commands · Ctrl+S sessions · Ctrl+N new · Ctrl+C interrupt/exit";
    chat_area_.append_system(oss.str(), /*plain=*/true);
}

void TuiApp::request_redraw() {
    // FTXUI only redraws when an animation frame is pending. Closures posted
    // from the agent thread do not invalidate the screen by themselves, so we
    // request one here, coalesced to keep the redraw rate bounded.
    constexpr auto kMinInterval = std::chrono::milliseconds(100);
    const auto now = std::chrono::steady_clock::now();
    if (last_redraw_request_ == std::chrono::steady_clock::time_point{} ||
        now - last_redraw_request_ >= kMinInterval) {
        last_redraw_request_ = now;
        ftxui::animation::RequestAnimationFrame();
    }
}

// ---------------------------------------------------------------------------
// Component tree
// ---------------------------------------------------------------------------

void TuiApp::build_component_tree() {
    using namespace ftxui;

    // Spinner component drives RequestAnimationFrame() for animated frames.
    spinner_component_ = make_spinner(status_bar_.spinner_state());

    // Only the InputBar participates in the focus chain, so keystrokes land
    // directly in the input field. Chat/status/top are pure renderers.
    auto container = Container::Vertical({
        input_bar_.component(),
    });

    auto main_layout = Renderer(container, [this] {
        auto& theme = default_theme();
        return vbox({
                   top_bar_.component()->Render()
                       | size(HEIGHT, EQUAL, 1),
                   separator() | color(theme.color.border_soft),
                   chat_area_.component()->Render() | flex | yframe
                       | vscroll_indicator,
                   separator() | color(theme.color.border_soft),
                   status_bar_.component()->Render()
                       | size(HEIGHT, EQUAL, 1),
                   separator() | color(theme.color.border_soft),
                   input_bar_.component()->Render()
                       | size(HEIGHT, EQUAL, 1),
               })
            | bgcolor(theme.color.bg);
    });

    main_layout->Add(spinner_component_);

    // Sidebar shown/hidden without stealing focus (see previous design notes).
    auto with_sidebar_events = CatchEvent(main_layout,
        [this](Event event) {
            if (sidebar_.is_showing()) {
                return sidebar_.component()->OnEvent(event);
            }
            return false;
        });

    root_component_ = Renderer(with_sidebar_events, [this, main_layout] {
        using namespace ftxui;
        if (sidebar_.is_showing() && sidebar_width_ > 0) {
            return hbox({
                sidebar_.component()->Render()
                    | size(WIDTH, EQUAL, sidebar_width_),
                separator() | color(default_theme().color.border_soft),
                main_layout->Render() | xflex,
            });
        }
        return main_layout->Render();
    });

    // Modal: approval dialog
    approval_showing_ = approval_handler_.is_showing();
    root_component_ = Modal(root_component_, approval_dialog_.component(),
                            &approval_showing_);

    // Modal: command palette
    palette_showing_ = command_palette_.is_showing();
    root_component_ = Modal(root_component_, command_palette_.component(),
                            &palette_showing_);

    // Global key bindings
    root_component_ = CatchEvent(root_component_, [this](Event event) {
        return handle_global_event(event);
    });
}

bool TuiApp::handle_global_event(ftxui::Event event) {
        using namespace ftxui;
        approval_showing_ = approval_handler_.is_showing();
        palette_showing_ = command_palette_.is_showing();

        // Mouse wheel scrolls the transcript. Leave events over the sidebar
        // to the sidebar when it is open.
        if (event.is_mouse() &&
            (event.mouse().button == ftxui::Mouse::WheelUp ||
             event.mouse().button == ftxui::Mouse::WheelDown)) {
            if (!(sidebar_.is_showing() && event.mouse().x < sidebar_width_) &&
                chat_area_.on_event(event)) {
                return true;
            }
        }

        // Ctrl+P: command palette
        if (event == Event::CtrlP) {
            if (command_palette_.is_showing()) {
                command_palette_.hide();
            } else {
                command_palette_.show();
            }
            return true;
        }
        // Ctrl+S: session sidebar
        if (event == Event::CtrlS) {
            sidebar_.toggle();
            sidebar_width_ = sidebar_.is_showing() ? 32 : 0;
            return true;
        }
        // Ctrl+N: start a new session
        if (event == Event::CtrlN) {
            start_new_session();
            return true;
        }
        // Ctrl+L: clear chat
        if (event == Event::CtrlL) {
            chat_area_.clear();
            return true;
        }
        // Ctrl+C: interrupt agent or exit
        if (event == Event::CtrlC) {
            if (agent_busy_.load()) {
                {
                    std::lock_guard<std::mutex> lock(pending_mutex_);
                    pending_inputs_.clear();
                }
                if (loop_) loop_->interrupt();
                return true;
            }
            screen_.Exit();
            return true;
        }
        return false;
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
    if (agent_busy_.load()) {
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            pending_inputs_.push_back(input);
        }
        chat_area_.append_system("⏎ 已排队，将在当前回复结束后自动发送");
        request_redraw();
        return;
    }
    chat_area_.append_user(input);
    run_agent(input);
}

void TuiApp::run_agent(const std::string& input) {
    if (!loop_) return;

    if (agent_thread_.joinable()) {
        agent_thread_.join();
    }

    agent_thread_ = std::thread([this, input] {
        agent_busy_.store(true);
        screen_.Post([this] {
            input_bar_.set_busy(true);
            status_bar_.set_busy(true);
            top_bar_.set_busy(true);
            request_redraw();
        });

        // Run the submitted turn, then any turns queued while it was busy
        // (Enter during output queues the message instead of dropping it).
        std::string current = input;
        for (;;) {
            auto result = loop_->run(current);

            if (budget_tracker_ && !loop_->conversation_id().empty()) {
                budget_tracker_->set_session_id(loop_->conversation_id());
            }

            if (!result.ok()) {
                std::string err_msg = result.error().message;
                screen_.Post([this, err_msg] {
                    chat_area_.append_error(err_msg);
                    request_redraw();
                });
                EA_ERROR("Agent error: {}", err_msg);
            }

            std::string next;
            {
                std::lock_guard<std::mutex> lock(pending_mutex_);
                if (!pending_inputs_.empty()) {
                    next = std::move(pending_inputs_.front());
                    pending_inputs_.pop_front();
                }
            }
            if (next.empty()) break;

            current = std::move(next);
            screen_.Post([this, current] {
                chat_area_.append_user(current);
                request_redraw();
            });
        }

        agent_busy_.store(false);
        screen_.Post([this] {
            input_bar_.set_busy(false);
            status_bar_.set_busy(false);
            top_bar_.set_busy(false);
            std::string sid = loop_->conversation_id();
            status_bar_.set_session_id(sid);
            top_bar_.set_session_id(sid);
            sidebar_.set_active(sid);
            request_redraw();
        });
    });
}

void TuiApp::execute_command(const std::string& cmd) {
    if (cmd == "/quit" || cmd == "/exit") {
        screen_.Exit();
        return;
    }
    if (cmd == "/new") {
        start_new_session();
        return;
    }
    if (cmd == "/compress" || cmd.substr(0, 10) == "/compress ") {
        if (!loop_) {
            chat_area_.append_error("Agent not ready");
            request_redraw();
            return;
        }
        if (agent_busy_.load()) {
            chat_area_.append_error("Cannot compress while the agent is busy");
            request_redraw();
            return;
        }
        std::string focus = cmd.size() > 10 ? cmd.substr(10) : "";
        while (!focus.empty() && (focus.front() == ' ' || focus.front() == '\n')) {
            focus.erase(focus.begin());
        }
        auto result = loop_->compress_context(focus);
        if (!result.ok()) {
            chat_area_.append_error("Compression failed: " + result.error().message);
        } else if (result.value().compressed) {
            chat_area_.append_system(
                "Context compressed: " + std::to_string(result.value().before)
                + " -> " + std::to_string(result.value().after) + " messages");
            sidebar_.refresh();
        } else {
            chat_area_.append_system("Nothing to compress — context is within budget");
        }
        request_redraw();
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
            oss << "Session:  " << su.total_tokens() << " tokens ("
                << su.input_tokens << " in / " << su.output_tokens << " out)\n"
                << "Global:   " << gu.total_tokens() << " tokens ("
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
            oss << "Session:  $" << sc.total() << "\n"
                << "Global:   $" << gc.total();
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
            while (!cid.empty() && (cid.back() == ' ' || cid.back() == '\n'))
                cid.pop_back();
            auto msgs = conv_store_->load(cid);
            if (msgs.ok()) {
                loop_->restore_conversation(cid, std::move(msgs.value()));
                auto meta = conv_store_->get_meta(cid);
                std::string title = meta.ok() ? meta.value().title : cid;
                chat_area_.append_system("Resumed conversation: " + title);
                status_bar_.set_session_id(cid);
                top_bar_.set_session_id(cid);
                sidebar_.set_active(cid);
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
    if (cmd == "/skills" || cmd.substr(0, 8) == "/skills ") {
        if (!skills_) {
            chat_area_.append_error("Skills system not available");
            return;
        }
        std::string query = cmd.size() > 8 ? cmd.substr(8) : "";
        while (!query.empty() && (query.front() == ' ' || query.front() == '\n')) {
            query.erase(query.begin());
        }
        auto list = skills_->list();
        if (!list.ok()) {
            chat_area_.append_error(list.error().message);
            return;
        }
        std::ostringstream oss;
        std::string last_category;
        int count = 0;
        for (const auto& info : list.value()) {
            if (!query.empty() && info.name.find(query) == std::string::npos &&
                info.description.find(query) == std::string::npos) {
                continue;
            }
            if (info.category != last_category) {
                last_category = info.category;
                oss << "  " << info.category << ":\n";
            }
            oss << "    - " << info.name;
            if (!info.description.empty()) oss << ": " << info.description;
            oss << "\n";
            ++count;
        }
        if (count == 0) {
            chat_area_.append_assistant("No skills found" +
                                        (query.empty() ? "" : " for: " + query));
        } else {
            chat_area_.append_assistant(
                "Available skills:\n" + oss.str() +
                "\nLoad one with /skill <name> or let the agent use skill_view.");
        }
        return;
    }
    if (cmd.substr(0, 7) == "/skill ") {
        if (!skills_) {
            chat_area_.append_error("Skills system not available");
            return;
        }
        std::string name = cmd.substr(7);
        while (!name.empty() && (name.back() == ' ' || name.back() == '\n')) {
            name.pop_back();
        }
        auto content = skills_->view(name);
        if (!content.ok()) {
            chat_area_.append_error(content.error().message);
            return;
        }
        chat_area_.append_user(cmd);
        run_agent("[Skill loaded: " + name + "]\n\n" + content.value() +
                  "\n\nFollow the skill instructions above.");
        return;
    }
    if (cmd == "/help") {
        chat_area_.append_assistant(
            "Commands:\n"
            "  /help         — Show this help\n"
            "  /new          — Start a new session\n"
            "  /compress     — Compress older context (optionally: /compress <focus>)\n"
            "  /usage        — Show token usage\n"
            "  /cost         — Show cost\n"
            "  /history      — List conversations\n"
            "  /resume <id>  — Resume a conversation\n"
            "  /export       — Export current conversation as JSONL\n"
            "  /import <path>— Import conversation from JSONL file\n"
            "  /skills       — List available skills\n"
            "  /skill <name> — Load a skill and follow its instructions\n"
            "  /clear        — Clear the chat view\n"
            "  /quit, /exit  — Exit the agent\n"
            "\n"
            "Keybindings:\n"
            "  Ctrl+P  — Command palette (type to filter)\n"
            "  Ctrl+S  — Session sidebar\n"
            "  Ctrl+N  — New session\n"
            "  Ctrl+L  — Clear chat\n"
            "  Ctrl+C  — Interrupt agent / exit");
        return;
    }

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

    sidebar_ = SessionSidebar(conv_store_);
    sidebar_.refresh();

    // Event listener — bridges AgentEvent to UI updates
    event_listener_ = std::make_shared<TuiEventListener>(
        chat_area_, status_bar_, top_bar_,
        [this](std::function<void()> fn) {
            screen_.Post([this, fn = std::move(fn)] {
                fn();
                request_redraw();
            });
        });
    loop.add_listener(event_listener_);

    // Trace listener — captures structured events to runtime-trace.jsonl
    if (auto trace_listener = ea::trace::create_listener()) {
        loop.add_listener(trace_listener);
    }

    // Wire callbacks
    input_bar_.set_on_submit([this](const std::string& input) {
        submit_input(input);
    });
    command_palette_.set_on_command([this](const std::string& cmd) {
        execute_command(cmd);
    });
    sidebar_.set_on_resume([this](std::string cid) {
        if (conv_store_) {
            auto msgs = conv_store_->load(cid);
            if (msgs.ok()) {
                loop_->restore_conversation(cid, std::move(msgs.value()));
                auto meta = conv_store_->get_meta(cid);
                std::string title = meta.ok() ? meta.value().title : cid;
                chat_area_.append_system("Resumed conversation: " + title);
                status_bar_.set_session_id(cid);
                top_bar_.set_session_id(cid);
                sidebar_.set_active(cid);
            } else {
                chat_area_.append_error("Conversation not found: " + cid);
            }
        }
    });

    // Identity
    status_bar_.set_model(model_);
    top_bar_.set_model(model_);
    char cwd_buf[4096];
    if (getcwd(cwd_buf, sizeof(cwd_buf))) {
        status_bar_.set_cwd(cwd_buf);
    }

    if (loop_) {
        std::string sid = loop_->conversation_id();
        status_bar_.set_session_id(sid);
        top_bar_.set_session_id(sid);
        sidebar_.set_active(sid);
    }

    push_banner();

    if (loop_ && !loop_->conversation_id().empty() && conv_store_) {
        auto meta = conv_store_->get_meta(loop_->conversation_id());
        std::string title = meta.ok() && !meta.value().title.empty()
            ? meta.value().title : loop_->conversation_id();
        chat_area_.append_system(
            "Resumed conversation: " + title + " ("
            + std::to_string(meta.ok() ? meta.value().message_count : 0)
            + " messages)");
        chat_area_.restore_history(loop_->history());
    }

    build_component_tree();

    // 1Hz heartbeat: keeps the busy indicator and elapsed timer live during
    // long waits. The redraw is posted so it executes on the UI thread.
    heartbeat_stop_.store(false);
    heartbeat_thread_ = std::thread([this] {
        while (!heartbeat_stop_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (agent_busy_.load()) {
                screen_.Post([this] { request_redraw(); });
            }
        }
    });

    screen_.Loop(root_component_);

    heartbeat_stop_.store(true);
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }

    // Cleanup: join agent thread if still running
    if (agent_thread_.joinable()) {
        if (loop_) loop_->interrupt();
        agent_thread_.join();
    }
}

void TuiApp::start_new_session() {
    if (!loop_) {
        chat_area_.append_error("Agent not ready");
        request_redraw();
        return;
    }
    if (agent_busy_.load()) {
        chat_area_.append_error("Cannot start a new session while the agent is busy");
        request_redraw();
        return;
    }

    loop_->clear_history();
    chat_area_.clear();
    chat_area_.append_system(
        "New session started — your next message will create a new conversation");
    status_bar_.set_session_id("");
    top_bar_.set_session_id("");
    status_bar_.reset_stats();
    sidebar_.set_active("");
    sidebar_.refresh();
    if (budget_tracker_) {
        budget_tracker_->reset_session();
        budget_tracker_->set_session_id("");
    }
    request_redraw();
}

}  // namespace ea::tui
