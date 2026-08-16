// CommandPalette — searchable slash-command menu (Modal overlay)
#pragma once
#include <ftxui/component/component.hpp>
#include <functional>
#include <string>
#include <vector>

namespace ea::tui {

struct CommandEntry {
    std::string name;         // e.g., "/quit"
    std::string description;  // e.g., "Exit the agent"
    std::string hint;         // optional key hint, e.g. "⌃C"
};

class CommandPalette {
public:
    CommandPalette();

    ftxui::Component component();
    bool is_showing() const;
    void show();
    void hide();
    void set_on_command(std::function<void(const std::string&)> fn);
    const std::vector<CommandEntry>& commands() const { return commands_; }
    void add_command(CommandEntry entry) { commands_.push_back(std::move(entry)); }
    void set_commands(std::vector<CommandEntry> commands) {
        commands_ = std::move(commands);
    }

private:
    ftxui::Component component_;
    std::vector<CommandEntry> commands_;
    std::string filter_;
    int selected_ = 0;
    bool showing_ = false;
    std::function<void(const std::string&)> on_command_;

    std::vector<int> filtered_indices() const;
    ftxui::Element render();
    bool on_event(ftxui::Event event);
};

}  // namespace ea::tui
