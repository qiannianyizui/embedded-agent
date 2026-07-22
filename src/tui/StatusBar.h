// StatusBar — bottom status bar showing token usage, cost, model, session, and busy indicator
#pragma once
#include <ftxui/component/component.hpp>
#include <string>

namespace ea::tui {

class StatusBar {
public:
    StatusBar();

    ftxui::Component component();
    void set_busy(bool busy);
    void update_usage(int input_tokens, int output_tokens);
    void update_cost(double cost_usd);
    void set_model(const std::string& model);
    void set_session_id(const std::string& id);

private:
    ftxui::Component component_;
    bool busy_ = false;
    int input_tokens_ = 0;
    int output_tokens_ = 0;
    double cost_usd_ = 0.0;
    std::string model_;
    std::string session_id_;
    int tick_ = 0;

    ftxui::Element render();
    static std::string format_tokens(int tokens);
    static std::string format_cost(double cost);
};

}  // namespace ea::tui
