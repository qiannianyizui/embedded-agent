// InputBar — bottom user input area with mode-aware prompt and hints
#pragma once
#include <ftxui/component/component.hpp>
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
    void clear();

private:
    ftxui::Component input_component_;
    ftxui::Component component_;
    std::string input_;
    std::vector<std::string> history_;
    int history_index_ = -1;
    bool busy_ = false;
    std::function<void(const std::string&)> on_submit_;
    std::string placeholder_;

    ftxui::Element render();
    bool on_event(ftxui::Event event);
};

}  // namespace ea::tui
