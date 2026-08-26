// Unit tests for SessionsDialog — modal session picker + resume
#include <catch2/catch_test_macros.hpp>
#include "tui/SessionsDialog.h"
#include "base/Types.h"
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

using namespace ea::tui;
using namespace ea::conversation;

namespace {

class FakeStore : public IConversationStore {
public:
    std::vector<ConversationMeta> sessions;

    ea::Result<std::string> create(const std::string&) override {
        return ea::Result<std::string>("conv_new");
    }
    ea::Result<void> append(const std::string&, const ea::Message&) override {
        return ea::Result<void>();
    }
    ea::Result<std::vector<ea::Message>> load(const std::string&) override {
        return ea::Result<std::vector<ea::Message>>(std::vector<ea::Message>{});
    }
    ea::Result<std::vector<ea::Message>> load_all(const std::string&) override {
        return ea::Result<std::vector<ea::Message>>(std::vector<ea::Message>{});
    }
    ea::Result<void> archive_and_compact(
        const std::string&, const std::vector<ea::Message>&) override {
        return ea::Result<void>();
    }
    ea::Result<std::vector<ConversationMeta>> list(int, int) override {
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
    ea::Result<std::string> import_jsonl(const std::string&,
                                         const std::string&) override {
        return ea::Result<std::string>("conv_imported");
    }
    ea::Result<void> open() override { return ea::Result<void>(); }
    ea::Result<void> close() override { return ea::Result<void>(); }
};

}  // namespace

TEST_CASE("SessionsDialog show renders session list", "[tui]") {
    FakeStore store;
    ConversationMeta meta;
    meta.id = "conv_abc123";
    meta.title = "Refactor the auth module";
    meta.message_count = 12;
    meta.updated_at = "2026-08-03T10:24:00Z";
    store.sessions.push_back(meta);

    SessionsDialog dialog;
    REQUIRE_FALSE(dialog.is_showing());

    dialog.set_store(&store);
    dialog.show();
    REQUIRE(dialog.is_showing());

    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(60),
                                        ftxui::Dimension::Fixed(12));
    ftxui::Render(screen, dialog.component()->Render());
    auto out = screen.ToString();

    REQUIRE(out.find("Refactor the auth module") != std::string::npos);
    REQUIRE(out.find("12 msgs") != std::string::npos);
}

TEST_CASE("SessionsDialog Enter resumes selected session and hides", "[tui]") {
    FakeStore store;
    ConversationMeta meta;
    meta.id = "conv_abc123";
    meta.title = "Refactor the auth module";
    store.sessions.push_back(meta);

    SessionsDialog dialog;
    dialog.set_store(&store);
    std::string resumed;
    dialog.set_on_resume([&](const std::string& id) { resumed = id; });

    dialog.show();
    REQUIRE(dialog.component()->OnEvent(ftxui::Event::Return));
    REQUIRE(resumed == "conv_abc123");
    REQUIRE_FALSE(dialog.is_showing());
}

TEST_CASE("SessionsDialog Escape closes without resuming", "[tui]") {
    FakeStore store;
    ConversationMeta meta;
    meta.id = "conv_abc123";
    meta.title = "Refactor the auth module";
    store.sessions.push_back(meta);

    SessionsDialog dialog;
    dialog.set_store(&store);
    bool resumed = false;
    dialog.set_on_resume([&](const std::string&) { resumed = true; });

    dialog.show();
    REQUIRE(dialog.component()->OnEvent(ftxui::Event::Escape));
    REQUIRE_FALSE(resumed);
    REQUIRE_FALSE(dialog.is_showing());
}

TEST_CASE("SessionsDialog arrows move selection when mounted via Modal", "[tui]") {
    // Regression test. In the real app the dialog is mounted as a Modal, and
    // Modal routes events through its internal Container::Tab, which refuses to
    // dispatch unless the active tab child is Focusable(). A plain
    // Renderer(lambda) is not Focusable(), so every keypress was dropped before
    // reaching on_event and ↑/↓ never moved the selection. The tests that call
    // OnEvent() directly on the dialog bypass the Modal and miss this, so this
    // test exercises the exact build_component_tree wiring.
    FakeStore store;
    for (int i = 0; i < 5; ++i) {
        ConversationMeta meta;
        meta.id = "conv_" + std::to_string(i);
        meta.title = "Session " + std::to_string(i);
        meta.message_count = i;
        store.sessions.push_back(meta);
    }

    SessionsDialog dialog;
    dialog.set_store(&store);
    dialog.show();

    // Mirror TuiApp::build_component_tree: a focusable main component with the
    // dialog mounted on top via Modal.
    std::string input_content;
    std::string placeholder;
    auto main = ftxui::Container::Vertical({
        ftxui::Input(&input_content, &placeholder),
    });
    bool modal_showing = dialog.is_showing();
    auto root = ftxui::Modal(main, dialog.component(), &modal_showing);

    std::string resumed;
    dialog.set_on_resume([&](const std::string& id) { resumed = id; });

    // Arrow keys must be handled by the dialog (through the Modal) and move the
    // selection: 0 -> 1 -> 2, then resume the third entry.
    REQUIRE(root->OnEvent(ftxui::Event::ArrowDown));
    REQUIRE(root->OnEvent(ftxui::Event::ArrowDown));
    REQUIRE(root->OnEvent(ftxui::Event::Return));
    REQUIRE(resumed == "conv_2");

    // And back: 1 -> 0 (ArrowUp), then 0 -> 1 (ArrowDown), resume the second.
    dialog.show();
    resumed.clear();
    REQUIRE(root->OnEvent(ftxui::Event::ArrowDown));  // 0 -> 1
    REQUIRE(root->OnEvent(ftxui::Event::ArrowUp));    // 1 -> 0
    REQUIRE(root->OnEvent(ftxui::Event::ArrowDown));  // 0 -> 1
    REQUIRE(root->OnEvent(ftxui::Event::Return));
    REQUIRE(resumed == "conv_1");
}