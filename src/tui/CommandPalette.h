// CommandPalette — slash command menu using FTXUI Menu + Modal
#pragma once
#include <ftxui/component/component.hpp>
#include <functional>
#include <string>
#include <vector>

namespace ea::tui {

struct CommandEntry {
    std::string name;         // e.g., "/quit"
    std::string description;  // e.g., "Exit the agent"
};

class CommandPalette {
public:
    CommandPalette();

    ftxui::Component component();
    bool is_showing() const;
    void show();
    void hide();
    void set_on_command(std::function<void(const std::string&)> fn);

private:
    ftxui::Component component_;
    std::vector<CommandEntry> commands_;
    int selected_ = 0;
    bool showing_ = false;
    std::function<void(const std::string&)> on_command_;

    ftxui::Element render();
    bool on_event(ftxui::Event event);
};

}  // namespace ea::tui
