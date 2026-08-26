// InputBar — bottom user input area with mode-aware prompt and hints
#pragma once
#include <ftxui/component/component.hpp>
#include "CommandPalette.h"
#include <functional>
#include <string>
#include <vector>

namespace ea::tui {

class InputBar {
public:
    InputBar();

    ftxui::Component component();
    void set_busy(bool busy);
    void set_on_submit(std::function<void(const std::string&)> fn);
    void set_on_command(std::function<void(const std::string&)> fn);
    void set_commands(std::vector<CommandEntry> commands);
    void set_mode(const std::string& mode);
    const std::string& mode() const { return mode_; }
    void set_cwd(const std::string& cwd);
    void clear();
    bool suggestions_visible() const;
    int command_area_height() const;

private:
    static constexpr int kMaxShown = 8;
    ftxui::Component input_component_;
    ftxui::Component component_;
    std::string input_;
    std::vector<std::string> history_;
    int history_index_ = -1;
    bool busy_ = false;
    std::function<void(const std::string&)> on_submit_;
    std::function<void(const std::string&)> on_command_;
    std::vector<CommandEntry> commands_;
    int selected_ = 0;
    int scroll_ = 0;
    std::string placeholder_;
    std::string mode_ = "manual";  // permission mode name (always shown)
    std::string cwd_;              // workspace path, shown before the mode hint

    ftxui::Element render();
    ftxui::Element mode_hint() const;
    std::vector<int> filtered_commands() const;
    bool on_event(ftxui::Event event);
};

}  // namespace ea::tui
