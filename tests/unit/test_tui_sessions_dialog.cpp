// Unit tests for SessionsDialog — modal session picker + resume
#include <catch2/catch_test_macros.hpp>
#include "tui/SessionsDialog.h"
#include "base/Types.h"
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