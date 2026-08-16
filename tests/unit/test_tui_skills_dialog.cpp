// Unit tests for SkillsDialog — skill enable/disable manager
#include <catch2/catch_test_macros.hpp>
#include "tui/SkillsDialog.h"
#include "skill/Skill.h"
#include "io/FileSystem.h"
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <filesystem>

using namespace ea::tui;
namespace fsys = std::filesystem;

TEST_CASE("SkillsDialog shows disabled skills and toggles with Enter", "[tui]") {
    auto base = fsys::temp_directory_path() / "ea_skills_dialog_test";
    fsys::remove_all(base);
    fsys::create_directories(base / "a");
    fsys::create_directories(base / "b");
    ea::fs::write_file((base / "a" / "SKILL.md").string(),
                       "---\nname: a\ndescription: Skill A\n---\n# A\n");
    ea::fs::write_file((base / "b" / "SKILL.md").string(),
                       "---\nname: b\ndescription: Skill B\n---\n# B\n");

    ea::skill::SkillManager manager({base.string()});
    REQUIRE(manager.disable("a").ok());

    auto all = manager.list_all();
    REQUIRE(all.ok());
    REQUIRE(all.value().size() == 2);
    REQUIRE(all.value()[0].enabled == false);

    SkillsDialog dialog;
    dialog.set_manager(&manager);
    dialog.show();
    REQUIRE(dialog.is_showing());

    auto render = [&] {
        auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                            ftxui::Dimension::Fixed(12));
        ftxui::Render(screen, dialog.component()->Render());
        return screen.ToString();
    };

    auto out = render();
    REQUIRE(out.find("[ ]") != std::string::npos);
    REQUIRE(out.find("[x]") != std::string::npos);
    REQUIRE(out.find("a") != std::string::npos);
    REQUIRE(out.find("b") != std::string::npos);

    // Enter toggles the selected skill (a, disabled -> enabled).
    dialog.component()->OnEvent(ftxui::Event::Return);
    REQUIRE(manager.list_all().value()[0].enabled);
    out = render();
    REQUIRE(out.find("a") != std::string::npos);

    // ArrowDown then Enter disables b.
    dialog.component()->OnEvent(ftxui::Event::ArrowDown);
    dialog.component()->OnEvent(ftxui::Event::Return);
    REQUIRE_FALSE(manager.list_all().value()[1].enabled);

    dialog.component()->OnEvent(ftxui::Event::Escape);
    REQUIRE_FALSE(dialog.is_showing());

    fsys::remove_all(base);
}
