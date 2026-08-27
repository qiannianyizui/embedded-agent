// TuiApp — assembles all FTXUI components and drives the TUI event loop
#include "TuiApp.h"
#include "log/Logger.h"
#include "trace/Trace.h"
#include "Theme.h"
#include "FormatUtils.h"
#include "agent/PermissionMode.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/animation.hpp>
#include <ftxui/screen/terminal.hpp>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <unistd.h>  // getcwd

namespace ea::tui {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

TuiApp::TuiApp() {
    base_commands_ = command_palette_.commands();
    input_bar_.set_commands(base_commands_);
    input_bar_.set_on_command([this](const std::string& cmd) {
        execute_command(cmd);
    });
}

TuiApp::~TuiApp() {
    heartbeat_stop_.store(true);
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
    if (plugin_thread_.joinable()) {
        plugin_thread_.join();
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
        << "\n  /help commands · Ctrl+C interrupt/exit";
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
        using namespace ftxui;
        auto& theme = default_theme();

        // Layout hints for ChatArea, refreshed every frame. The chat column
        // width drives the (memoized) line wrapping; the viewport height
        // bounds which messages are constructed per frame (virtualization).
        const auto dims = Terminal::Size();
        int chat_width = dims.dimx - 4;  // left/right margins
        int input_h = 1;
        if (input_bar_.suggestions_visible()) {
            input_h += input_bar_.command_area_height();
        }
        input_h += 1;  // mode hint line below the input row
        // Conservative wrap width: user bubbles are the narrowest message
        // layout (2+2 padding + 2 border), so wrapping at this width never
        // clips any message body.
        chat_area_.set_layout_width(std::max(1, chat_width - 6));
        chat_area_.set_viewport_hint(std::max(1, dims.dimy - 5 - input_h));

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
                   input_bar_.component()->Render(),
               })
            | bgcolor(theme.color.bg);
    });

    main_layout->Add(spinner_component_);

    root_component_ = main_layout;

    // Modal: approval dialog
    approval_showing_ = approval_handler_.is_showing();
    root_component_ = Modal(root_component_, approval_dialog_.component(),
                            &approval_showing_);

    // Modal: command palette
    palette_showing_ = command_palette_.is_showing();
    root_component_ = Modal(root_component_, command_palette_.component(),
                            &palette_showing_);

    // Modal: skills manager
    skills_showing_ = skills_dialog_.is_showing();
    root_component_ = Modal(root_component_, skills_dialog_.component(),
                            &skills_showing_);

    // Modal: session picker
    sessions_showing_ = sessions_dialog_.is_showing();
    root_component_ = Modal(root_component_, sessions_dialog_.component(),
                            &sessions_showing_);

    // Global key bindings
    root_component_ = CatchEvent(root_component_, [this](Event event) {
        return handle_global_event(event);
    });
}

bool TuiApp::handle_global_event(ftxui::Event event) {
        using namespace ftxui;
        approval_showing_ = approval_handler_.is_showing();
        palette_showing_ = command_palette_.is_showing();
        skills_showing_ = skills_dialog_.is_showing();
        sessions_showing_ = sessions_dialog_.is_showing();

        // Mouse wheel scrolls the transcript.
        if (event.is_mouse() &&
            (event.mouse().button == ftxui::Mouse::WheelUp ||
             event.mouse().button == ftxui::Mouse::WheelDown)) {
            if (input_bar_.suggestions_visible()) {
                auto dims = ftxui::Terminal::Size();
                if (event.mouse().y >= dims.dimy - input_bar_.command_area_height()) {
                    return input_bar_.component()->OnEvent(event);
                }
            }
            if (chat_area_.on_event(event)) {
                return true;
            }
        }

        // Shift+Tab: cycle permission mode (default → acceptEdits → plan).
        // Tab is command completion.
        if (event == Event::TabReverse) {
            cycle_permission_mode();
            return true;
        }
        // Ctrl+O: toggle verbose transcript (expanded tool cards).
        if (event == Event::CtrlO) {
            chat_area_.set_verbose(!chat_area_.verbose());
            request_redraw();
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
            notify_session_end();
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
    // Slash commands are single-line; a multi-line input starting with '/'
    // is sent to the agent as a normal message.
    if (input[0] == '/' && input.find('\n') == std::string::npos) {
        execute_command(input);
        return;
    }
    if (agent_busy_.load()) {
        bool queued = false;
        bool echo_now = false;
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            // Re-check under the lock: the agent thread clears busy inside
            // this mutex, so a false read here means it already drained.
            if (agent_busy_.load()) {
                echo_now = turn_output_done_.load();
                pending_inputs_.push_back({input, echo_now});
                queued = true;
            }
        }
        if (queued) {
            if (echo_now) {
                // The reply is fully visible; only post-turn work remains.
                // Echo the message now so it doesn't look swallowed.
                chat_area_.append_user(input);
                chat_area_.append_system("⏎ 已接收，正在整理记忆，稍后自动发送");
            } else {
                chat_area_.append_system("⏎ 已排队，将在当前回复结束后自动发送");
            }
            request_redraw();
            return;
        }
        // Agent freed up between the two checks — fall through and run now.
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

            bool have_next = false;
            PendingInput next;
            {
                std::lock_guard<std::mutex> lock(pending_mutex_);
                if (!pending_inputs_.empty()) {
                    next = std::move(pending_inputs_.front());
                    pending_inputs_.pop_front();
                    have_next = true;
                } else {
                    // Clear busy inside the mutex so submit_input can
                    // safely decide between queueing and running directly.
                    agent_busy_.store(false);
                    turn_output_done_.store(true);
                }
            }
            if (!have_next) break;

            current = std::move(next.text);
            if (!next.echoed) {
                screen_.Post([this, current] {
                    chat_area_.append_user(current);
                    request_redraw();
                });
            }
        }

        agent_busy_.store(false);
        turn_output_done_.store(true);
        screen_.Post([this] {
            input_bar_.set_busy(false);
            status_bar_.set_busy(false);
            top_bar_.set_busy(false);
            std::string sid = loop_->conversation_id();
            status_bar_.set_session_id(sid);
            top_bar_.set_session_id(sid);
            request_redraw();
        });
    });
}

void TuiApp::run_plugin_async(std::string status,
                              std::function<Result<std::string>()> op,
                              const std::string& success_prefix,
                              const std::string& success_suffix) {
    if (plugin_busy_.exchange(true)) {
        chat_area_.append_error("A plugin operation is already running");
        request_redraw();
        return;
    }
    chat_area_.append_system(std::move(status));
    request_redraw();

    if (plugin_thread_.joinable()) plugin_thread_.join();
    plugin_thread_ = std::thread([this, op = std::move(op),
                                  success_prefix, success_suffix] {
        auto result = op();
        auto ok = result.ok();
        std::string message = ok
            ? success_prefix + result.value() + success_suffix
            : result.error().message;
        screen_.Post([this, ok, message] {
            if (ok) {
                chat_area_.append_assistant(message);
            } else {
                chat_area_.append_error(message);
            }
            request_redraw();
        });
        plugin_busy_.store(false);
    });
}

void TuiApp::rebuild_skill_commands() {
    std::vector<CommandEntry> commands = base_commands_;
    if (skills_) {
        auto list = skills_->list();
        if (list.ok()) {
            for (const auto& info : list.value()) {
                std::string name = info.alias.empty() ? info.name : info.alias;
                commands.push_back({"/" + name, info.description, ""});
            }
        }
    }
    command_palette_.set_commands(commands);
    input_bar_.set_commands(commands);
}

void TuiApp::execute_command(const std::string& cmd) {
    if (cmd == "/quit" || cmd == "/exit") {
        notify_session_end();
        screen_.Exit();
        return;
    }
    if (cmd == "/new") {
        start_new_session();
        return;
    }
    if (cmd == "/compact" || cmd.substr(0, 8) == "/compact ") {
        if (!loop_) {
            chat_area_.append_error("Agent not ready");
            request_redraw();
            return;
        }
        if (agent_busy_.load()) {
            chat_area_.append_error("Cannot compact while the agent is busy");
            request_redraw();
            return;
        }
        std::string focus = cmd.size() > 8 ? cmd.substr(8) : "";
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
    if (cmd == "/resume" || cmd.substr(0, 8) == "/resume ") {
        std::string cid = cmd.size() > 8 ? cmd.substr(8) : "";
        while (!cid.empty() && (cid.back() == ' ' || cid.back() == '\n'))
            cid.pop_back();
        if (cid.empty()) {
            if (!conv_store_) {
                chat_area_.append_error("Conversation persistence not available");
                request_redraw();
                return;
            }
            sessions_dialog_.set_active(loop_ ? loop_->conversation_id() : "");
            sessions_dialog_.show();
            sessions_showing_ = true;
            request_redraw();
            return;
        }
        resume_session(cid);
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
    if (cmd == "/skills") {
        if (!skills_) {
            chat_area_.append_error("Skills system not available");
            return;
        }
        skills_dialog_.set_manager(skills_);
        skills_dialog_.show();
        skills_showing_ = true;
        request_redraw();
        return;
    }
    if (cmd.substr(0, 8) == "/skills ") {
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
                "\nEvery skill is a command, e.g. /superpowers:brainstorming.");
        }
        return;
    }
    if (cmd == "/plugin" || cmd.substr(0, 8) == "/plugin ") {
        if (!plugins_) {
            chat_area_.append_error("Plugin system not available");
            return;
        }
        std::string args = cmd == "/plugin" ? "" : cmd.substr(8);
        while (!args.empty() &&
               (args.front() == ' ' || args.front() == '\n')) {
            args.erase(args.begin());
        }
        while (!args.empty() && (args.back() == ' ' || args.back() == '\n')) {
            args.pop_back();
        }

        if (args.empty() || args == "list") {
            auto list = plugins_->list();
            if (!list.ok()) {
                chat_area_.append_error(list.error().message);
                return;
            }
            if (list.value().empty()) {
                chat_area_.append_assistant(
                    "No plugins installed.\n"
                    "Install with: /plugin install <name-or-url>");
                return;
            }
            std::ostringstream oss;
            oss << "Installed plugins:\n";
            for (const auto& info : list.value()) {
                oss << "  - " << info.name << "\n";
            }
            oss << "\nNewly installed plugins activate after restart.";
            chat_area_.append_assistant(oss.str());
            return;
        }
        if (args == "marketplace" || args.rfind("marketplace ", 0) == 0) {
            auto sub = args == "marketplace" ? "" : args.substr(12);
            while (!sub.empty() && sub.front() == ' ') sub.erase(sub.begin());
            if (sub.empty() || sub == "list") {
                auto list = plugins_->list_marketplaces();
                if (!list.ok()) {
                    chat_area_.append_error(list.error().message);
                    return;
                }
                if (list.value().empty()) {
                    chat_area_.append_assistant(
                        "No marketplaces registered.\n"
                        "Register with: /plugin marketplace add <owner/repo>");
                    return;
                }
                std::ostringstream oss;
                oss << "Registered marketplaces:\n";
                for (const auto& m : list.value()) {
                    oss << "  - " << m.name << "\n";
                }
                chat_area_.append_assistant(oss.str());
                return;
            }
            if (sub.rfind("add ", 0) == 0) {
                auto source = sub.substr(4);
                run_plugin_async(
                    "⏳ 正在注册 marketplace: " + source + " ...",
                    [this, source] {
                        return plugins_->add_marketplace(source);
                    },
                    "已注册 marketplace: ");
                return;
            }
            if (sub.rfind("remove ", 0) == 0) {
                auto name = sub.substr(7);
                auto result = plugins_->remove_marketplace(name);
                if (!result.ok()) {
                    chat_area_.append_error(result.error().message);
                    return;
                }
                chat_area_.append_assistant("Removed marketplace: " + name);
                return;
            }
            chat_area_.append_error(
                "Usage: /plugin marketplace [list|add <owner/repo>|remove <name>]");
            return;
        }
        if (args.rfind("install ", 0) == 0) {
            auto source = args.substr(8);
            run_plugin_async(
                "⏳ 正在安装插件: " + source + " ...",
                [this, source] { return plugins_->install(source); },
                "已安装插件: ", "\n重启后生效。");
            return;
        }
        if (args.rfind("remove ", 0) == 0) {
            auto name = args.substr(7);
            auto result = plugins_->remove(name);
            if (!result.ok()) {
                chat_area_.append_error(result.error().message);
                return;
            }
            chat_area_.append_assistant("Removed plugin: " + name);
            return;
        }
        chat_area_.append_error(
            "Usage: /plugin [list|install <name-or-url>|remove <name>]");
        return;
    }
    if (cmd == "/help") {
        chat_area_.append_assistant(
            "Commands:\n"
            "  /help         — Show this help\n"
            "  /new          — Start a new session\n"
            "  /compact      — Compact older context (optionally: /compact <focus>)\n"
            "  /usage        — Show token usage\n"
            "  /cost         — Show cost\n"
            "  /resume       — Resume or switch conversations\n"
            "  /export       — Export current conversation as JSONL\n"
            "  /import <path>— Import conversation from JSONL file\n"
            "  /skills       — Manage skills (enable/disable)\n"
            "  /<skill>      — Invoke a skill as a command, e.g. /superpowers:brainstorming\n"
            "  /plugin       — List, install, or remove plugins\n"
            "  /plugin marketplace — Register plugin marketplaces\n"
            "  /clear        — Clear the chat view\n"
            "  /quit, /exit  — Exit the agent\n"
            "\n"
            "Modes:\n"
            "  Shift+Tab cycles permission mode: manual → acceptEdits → plan → auto.\n"
            "  The active mode is always shown below the input box.\n"
            "\n"
            "Keybindings:\n"
            "  Shift+Tab — Cycle permission mode (manual/acceptEdits/plan/auto)\n"
            "  Tab       — Complete command from suggestions\n"
            "  Ctrl+O    — Expand/collapse tool call details\n"
            "  Ctrl+C    — Interrupt agent / exit");
        return;
    }

    // Any other "/name [topic...]" is treated as a skill command
    // (e.g. /superpowers:brainstorming <idea>).
    if (cmd.size() > 1 && cmd[0] == '/' && skills_) {
        auto space = cmd.find(' ');
        std::string name = space == std::string::npos
                               ? cmd.substr(1)
                               : cmd.substr(1, space - 1);
        auto content = skills_->view(name);
        if (content.ok()) {
            std::string topic = space == std::string::npos ? "" : cmd.substr(space + 1);
            while (!topic.empty() &&
                   (topic.front() == ' ' || topic.front() == '\n')) {
                topic.erase(topic.begin());
            }
            chat_area_.append_user(cmd);
            std::string prompt = "[Skill loaded: " + name + "]\n\n" +
                                 content.value() +
                                 "\n\nFollow the skill instructions above.";
            if (!topic.empty()) {
                prompt += "\n\nUser's task:\n" + topic;
            }
            run_agent(prompt);
            return;
        }
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

    // Match the terminal's default colors to the dark theme so empty cells
    // (initial frame, resize, scroll redraws) don't flash white on
    // light-background terminals.
    std::cout << "\x1b]10;rgb:e8/ea/f2\x1b\\"
              << "\x1b]11;rgb:0d/11/17\x1b\\"
              << std::flush;

    // Event listener — bridges AgentEvent to UI updates
    event_listener_ = std::make_shared<TuiEventListener>(
        chat_area_, status_bar_, top_bar_, input_bar_,
        [this](std::function<void()> fn) {
            screen_.Post([this, fn = std::move(fn)] {
                fn();
                request_redraw();
            });
        });
    event_listener_->set_on_turn_start([this] { turn_output_done_.store(false); });
    event_listener_->set_on_turn_end([this] { turn_output_done_.store(true); });
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
    sessions_dialog_.set_store(conv_store_);
    sessions_dialog_.set_on_resume([this](std::string cid) {
        resume_session(cid);
    });

    skills_dialog_.set_manager(skills_);
    skills_dialog_.set_on_changed([this] { rebuild_skill_commands(); });
    rebuild_skill_commands();

    // Identity
    status_bar_.set_model(model_);
    top_bar_.set_model(model_);
    char cwd_buf[4096];
    if (getcwd(cwd_buf, sizeof(cwd_buf))) {
        status_bar_.set_cwd(cwd_buf);
        input_bar_.set_cwd(cwd_buf);
    }

    if (loop_) {
        std::string sid = loop_->conversation_id();
        status_bar_.set_session_id(sid);
        top_bar_.set_session_id(sid);
        sessions_dialog_.set_active(sid);
        sync_mode_ui();
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

    // Restore the terminal's original default colors.
    std::cout << "\x1b]110\x1b\\"
              << "\x1b]111\x1b\\"
              << std::flush;

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

// Fire-and-forget: hand the current session to the background memory
// extractor (no-op when extraction is unavailable).
void TuiApp::notify_session_end() {
    if (!memory_extractor_ || !loop_) return;
    const std::string& cid = loop_->conversation_id();
    if (cid.empty()) return;
    memory_extractor_->enqueue(cid);
}

void TuiApp::resume_session(const std::string& cid) {
    if (!conv_store_ || !loop_) {
        chat_area_.append_error("Conversation persistence not available");
        request_redraw();
        return;
    }
    if (agent_busy_.load()) {
        chat_area_.append_error("Cannot switch sessions while the agent is busy");
        request_redraw();
        return;
    }
    auto msgs = conv_store_->load(cid);
    if (!msgs.ok()) {
        chat_area_.append_error("Conversation not found: " + cid);
        request_redraw();
        return;
    }
    notify_session_end();
    loop_->restore_conversation(cid, std::move(msgs.value()));
    auto meta = conv_store_->get_meta(cid);
    std::string title = meta.ok() ? meta.value().title : cid;
    chat_area_.append_system("Resumed conversation: " + title);
    sync_mode_ui();
    status_bar_.set_session_id(cid);
    top_bar_.set_session_id(cid);
    request_redraw();
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

    notify_session_end();
    loop_->clear_history();
    if (loop_->plan_mode()) {
        loop_->set_plan_mode(false);
    }
    sync_mode_ui();
    chat_area_.clear();
    chat_area_.append_system(
        "New session started — your next message will create a new conversation");
    status_bar_.set_session_id("");
    top_bar_.set_session_id("");
    sessions_dialog_.set_active("");
    status_bar_.reset_stats();
    if (budget_tracker_) {
        budget_tracker_->reset_session();
        budget_tracker_->set_session_id("");
    }
    request_redraw();
}

// ---------------------------------------------------------------------------
// Permission-mode cycling (Shift+Tab)
// ---------------------------------------------------------------------------

void TuiApp::sync_mode_ui() {
    if (!loop_) return;
    const std::string mode_name = permission_mode_name(loop_->permission_mode());
    // The permission mode is only surfaced on the bottom input-bar line.
    input_bar_.set_mode(mode_name);
}

void TuiApp::cycle_permission_mode() {
    using ea::agent::PermissionMode;
    if (!loop_) return;
    if (agent_busy_.load()) {
        chat_area_.append_error("Cannot switch mode while the agent is busy");
        request_redraw();
        return;
    }

    // Shift+Tab cycle order: manual → acceptEdits → plan → auto.
    static constexpr PermissionMode kCycle[] = {
        PermissionMode::Manual, PermissionMode::AcceptEdits,
        PermissionMode::Plan, PermissionMode::BypassPermissions};
    constexpr size_t kCycleSize = sizeof(kCycle) / sizeof(kCycle[0]);

    const PermissionMode cur = loop_->permission_mode();
    PermissionMode next = kCycle[0];
    for (size_t i = 0; i < kCycleSize; ++i) {
        if (kCycle[i] == cur) next = kCycle[(i + 1) % kCycleSize];
    }

    auto result = loop_->set_permission_mode(next);
    if (!result.ok()) {
        chat_area_.append_error(result.error().message);
        request_redraw();
        return;
    }
    // Mode switches are silent: the new mode is already visible on the
    // input-bar mode line, so nothing is written to the transcript.
    sync_mode_ui();
    request_redraw();
}

}  // namespace ea::tui
