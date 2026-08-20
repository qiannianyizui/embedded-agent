// tui_preview — offline renderer for the redesigned TUI.
//
// Builds the same components used by TuiApp, fills them with representative
// data, renders each view to a fixed-size ftxui::Screen, and writes the ANSI
// output to <outdir>/tui_<view>.ans. A companion script converts the ANSI
// stream into a PNG screenshot.
//
//   cmake -B build -DEA_BUILD_TUI_PREVIEW=ON
//   cmake --build build --target tui-preview
//   ./build/src/tui/tui-preview /tmp/tui_preview

#include "tui/ChatArea.h"
#include "tui/InputBar.h"
#include "tui/StatusBar.h"
#include "tui/TopBar.h"
#include "tui/SessionsDialog.h"
#include "tui/CommandPalette.h"
#include "tui/ApprovalDialog.h"
#include "tui/TuiApprovalHandler.h"
#include "tui/Banner.h"
#include "tui/Theme.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/terminal.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

using namespace ea::tui;
using namespace ea::conversation;

namespace {

// ---------------------------------------------------------------------------
// Fake conversation store — supplies sample sidebar content
// ---------------------------------------------------------------------------
class FakeStore : public IConversationStore {
public:
    ea::Result<std::string> create(const std::string&) override {
        return ea::Result<std::string>("conv_fake");
    }
    ea::Result<void> append(const std::string&, const ea::Message&) override {
        return ea::Result<void>();
    }
    ea::Result<std::vector<ea::Message>> load(const std::string&) override {
        return ea::Result<std::vector<ea::Message>>(
            std::vector<ea::Message>{});
    }
    ea::Result<std::vector<ea::Message>> load_all(const std::string&) override {
        return ea::Result<std::vector<ea::Message>>(std::vector<ea::Message>{});
    }
    ea::Result<void> archive_and_compact(const std::string&,
                                         const std::vector<ea::Message>&) override {
        return ea::Result<void>();
    }
    ea::Result<std::vector<ConversationMeta>> list(int, int) override {
        std::vector<ConversationMeta> sessions;
        ConversationMeta a;
        a.id = "conv_a1b2c3d4";
        a.title = "Refactor the auth module";
        a.message_count = 12;
        a.updated_at = "2026-08-03T10:24:00Z";
        sessions.push_back(a);

        ConversationMeta b;
        b.id = "conv_e5f6a7b8";
        b.title = "Fix flaky integration tests";
        b.message_count = 7;
        b.updated_at = "2026-08-02T18:03:00Z";
        sessions.push_back(b);

        ConversationMeta c;
        c.id = "conv_c9d0e1f2";
        c.title = "Design the memory eviction policy";
        c.message_count = 3;
        c.updated_at = "2026-08-01T09:41:00Z";
        sessions.push_back(c);
        return ea::Result<std::vector<ConversationMeta>>(sessions);
    }
    ea::Result<ConversationMeta> get_meta(const std::string&) override {
        return ea::Result<ConversationMeta>(ConversationMeta{});
    }
    ea::Result<bool> remove(const std::string&) override {
        return ea::Result<bool>(true);
    }
    ea::Result<std::string> export_jsonl(const std::string&) override {
        return ea::Result<std::string>("");
    }
    ea::Result<std::string> import_jsonl(const std::string&, const std::string&) override {
        return ea::Result<std::string>("conv_imported");
    }
    ea::Result<void> open() override { return ea::Result<void>(); }
    ea::Result<void> close() override { return ea::Result<void>(); }
};

// ---------------------------------------------------------------------------
// Build the main screen (mirror of TuiApp::build_component_tree layout)
// ---------------------------------------------------------------------------
struct MainScreen {
    ChatArea chat;
    InputBar input;
    StatusBar status;
    TopBar top{status.spinner_state()};
    ftxui::Component root;

    explicit MainScreen(FakeStore*) {
        using namespace ftxui;
        auto& theme = default_theme();

        auto container = Container::Vertical({input.component()});
        root = Renderer(container, [&] {
            return vbox({
                       top.component()->Render() | size(HEIGHT, EQUAL, 1),
                       separator() | color(theme.color.border_soft),
                       chat.component()->Render() | flex | yframe
                           | vscroll_indicator,
                       separator() | color(theme.color.border_soft),
                       status.component()->Render() | size(HEIGHT, EQUAL, 1),
                       separator() | color(theme.color.border_soft),
                       input.component()->Render() | size(HEIGHT, EQUAL, 1),
                   })
                | bgcolor(theme.color.bg);
        });
    }

    void type_input(const std::string& text) {
        for (char c : text) {
            input.component()->OnEvent(ftxui::Event::Character(c));
        }
    }
};

void populate_idle_chat(ChatArea& chat) {
    chat.append_system(
        "◈ Embedded Agent\n"
        "  model      deepseek-chat\n"
        "  workspace  /home/lsy/embedded-agent\n"
        "  session    (new)\n"
        "\nWhat would you like to do?\n"
        "\n  /help commands · Ctrl+C interrupt/exit",
        /*plain=*/true);
}

void populate_working_chat(ChatArea& chat) {
    populate_idle_chat(chat);
    chat.append_user("Refactor the auth module to use a token bucket rate limiter.");

    ea::StreamChunk chunk;
    chunk.type = ea::StreamChunk::Type::Content;
    chunk.data =
        "I'll start by exploring the current auth implementation to understand "
        "how requests are handled today.";
    chat.append_stream_chunk(chunk);

    chat.append_tool_start("search_files", R"({"pattern":"auth","root":"src/"})");
    chat.append_tool_end("search_files",
                         "12 matches across 4 files\nsrc/auth/Guard.cpp  …",
                         false, 380);
    chat.append_tool_start("read_file",
                           R"({"path":"src/auth/Guard.cpp","lines":[1,120]})");
    chat.append_tool_end("read_file", "…token check happens in verify()…",
                         false, 210);
    chat.append_tool_start("patch",
                           R"({"file":"src/auth/Guard.cpp","edits":[...]})");
    chat.append_tool_end("patch", "patch failed: context mismatch at line 64",
                         true, 95);
    chat.append_error("Patch rejected — retrying with updated context…");
}

std::string render_view(ftxui::Component comp, int width, int height) {
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(width),
                                        ftxui::Dimension::Fixed(height));
    ftxui::Render(screen, comp->Render());
    return screen.ToString();
}

std::string render_element(const ftxui::Element& elem, int width, int height) {
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(width),
                                        ftxui::Dimension::Fixed(height));
    ftxui::Render(screen, elem);
    return screen.ToString();
}

void write_file(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
}

}  // anonymous namespace

int main(int argc, char** argv) {
    std::string outdir = argc > 1 ? argv[1] : "/tmp/tui_preview";
    std::filesystem::create_directories(outdir);

    // The offline renderer targets full-fidelity screenshots: force 24-bit
    // color support regardless of NO_COLOR/terminal detection. The real app
    // still respects the environment.
    ftxui::Terminal::SetColorSupport(ftxui::Terminal::Color::TrueColor);

    // ------------------------------------------------------------------
    // View 1: idle chat (full layout + sidebar)
    // ------------------------------------------------------------------
    {
        FakeStore store;
        MainScreen m(&store);
        populate_idle_chat(m.chat);
        m.status.set_model("deepseek-chat");
        m.top.set_model("deepseek-chat");
        m.status.set_cwd("/home/lsy/embedded-agent");
        m.status.set_session_id("");
        m.top.set_session_id("");
        m.status.update_usage(12480, 3210);
        m.status.update_cost(0.0123);
        m.status.set_context_pct(42);
        m.type_input("Refactor the auth module to use a token bucket");
        write_file(outdir + "/tui_idle.ans",
                   render_view(m.root, 110, 34));
    }

    // ------------------------------------------------------------------
    // View 2: busy chat (streaming + tool cards + error)
    // ------------------------------------------------------------------
    {
        FakeStore store;
        MainScreen m(&store);
        populate_working_chat(m.chat);
        m.status.set_model("deepseek-chat");
        m.top.set_model("deepseek-chat");
        m.status.set_cwd("/home/lsy/embedded-agent");
        m.status.set_session_id("conv_ab12cd34");
        m.top.set_session_id("conv_ab12cd34");
        m.status.update_usage(18480, 5210);
        m.status.update_cost(0.0231);
        m.status.set_context_pct(64);
        m.status.set_busy(true, "patching");
        m.top.set_busy(true, "patching");
        m.input.set_busy(true);
        write_file(outdir + "/tui_busy.ans",
                   render_view(m.root, 110, 34));
    }

    // ------------------------------------------------------------------
    // View 3: command palette (filtered)
    // ------------------------------------------------------------------
    {
        using namespace ftxui;
        CommandPalette palette;
        palette.show();
        // Type "re" to exercise live filtering
        palette.component()->OnEvent(Event::Character('r'));
        palette.component()->OnEvent(Event::Character('e'));
        auto& theme = default_theme();
        auto overlay = vbox({
            filler(),
            palette.component()->Render() | center,
            filler(),
        }) | bgcolor(theme.color.bg);
        write_file(outdir + "/tui_palette.ans",
                   render_element(overlay, 110, 26));
    }

    // ------------------------------------------------------------------
    // View 4: sessions dialog (/sessions)
    // ------------------------------------------------------------------
    {
        using namespace ftxui;
        FakeStore store;
        SessionsDialog dialog;
        dialog.set_store(&store);
        dialog.set_active("conv_a1b2c3d4");
        dialog.show();
        auto& theme = default_theme();
        auto overlay = vbox({
            filler(),
            dialog.component()->Render() | center,
            filler(),
        }) | bgcolor(theme.color.bg);
        write_file(outdir + "/tui_sessions.ans",
                   render_element(overlay, 110, 26));
    }

    // ------------------------------------------------------------------
    // View 5: approval dialog (high-risk tool)
    // ------------------------------------------------------------------
    {
        using namespace ftxui;
        TuiApprovalHandler handler;
        ApprovalDialog dialog(handler);
        ea::security::ApprovalRequest req{
            "run_command",
            nlohmann::json{{"cmd", "rm -rf build/cache"},
                           {"timeout", 30}},
            "Clean stale build artifacts before recompiling",
        };
        std::thread worker([&] { handler.request_approval(req); });
        while (!handler.is_showing()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        auto& theme = default_theme();
        auto overlay = vbox({
            filler(),
            dialog.component()->Render() | center,
            filler(),
        }) | bgcolor(theme.color.bg);
        write_file(outdir + "/tui_approval.ans",
                   render_element(overlay, 110, 24));
        handler.set_decision(ea::security::ApprovalDecision::Approved);
        worker.join();
    }

    std::cout << "wrote previews to " << outdir << "\n";
    return 0;
}
