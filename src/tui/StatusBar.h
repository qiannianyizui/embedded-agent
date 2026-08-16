// StatusBar — compact activity/usage strip above the input bar
#pragma once
#include <ftxui/component/component.hpp>
#include "Spinner.h"
#include <string>
#include <chrono>

namespace ea::tui {

class StatusBar {
public:
    StatusBar();

    ftxui::Component component();
    void set_busy(bool busy, const std::string& activity = "");
    void update_usage(int input_tokens, int output_tokens);
    void update_cost(double cost_usd);
    void reset_stats();
    void set_model(const std::string& model);
    void set_mode(const std::string& mode);
    void set_session_id(const std::string& id);
    void set_cwd(const std::string& cwd);
    void set_context_pct(int pct);  // 0-100 context usage percentage

    // Shared spinner state (drives animation for status bar and top bar)
    SpinnerState& spinner_state() { return spinner_state_; }

private:
    ftxui::Component component_;
    SpinnerState spinner_state_;
    bool busy_ = false;
    int input_tokens_ = 0;
    int output_tokens_ = 0;
    double cost_usd_ = 0.0;
    std::string model_;
    std::string mode_ = "build";
    std::string session_id_;
    std::string cwd_;
    int context_pct_ = -1;  // -1 = unknown
    std::chrono::steady_clock::time_point session_start_{};

    ftxui::Element render();
    ftxui::Color context_color() const;
};

}  // namespace ea::tui
