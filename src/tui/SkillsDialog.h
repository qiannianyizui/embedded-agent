// SkillsDialog — modal skill manager: list all skills, Enter toggles enable/disable
#pragma once
#include <ftxui/component/component.hpp>
#include "skill/Skill.h"
#include <functional>
#include <string>
#include <vector>

namespace ea::tui {

class SkillsDialog {
public:
    SkillsDialog();

    ftxui::Component component();
    void set_manager(ea::skill::SkillManager* manager);
    void set_on_changed(std::function<void()> fn);
    bool is_showing() const;
    void show();
    void hide();

private:
    ftxui::Component component_;
    ea::skill::SkillManager* skills_ = nullptr;
    std::vector<ea::skill::SkillInfo> entries_;
    std::function<void()> on_changed_;
    int selected_ = 0;
    bool showing_ = false;

    void refresh();
    ftxui::Element render();
    bool on_event(ftxui::Event event);
};

}  // namespace ea::tui
