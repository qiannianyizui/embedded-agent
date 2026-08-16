// TopBar — one-line app header: brand, status, model/session, key hints
#pragma once
#include <ftxui/component/component.hpp>
#include "Spinner.h"
#include <string>

namespace ea::tui {

class TopBar {
public:
    explicit TopBar(SpinnerState& spinner);

    ftxui::Component component();
    void set_busy(bool busy, const std::string& activity = "");
    void set_model(const std::string& model);
    void set_mode(const std::string& mode);
    void set_session_id(const std::string& id);

private:
    SpinnerState& spinner_;
    ftxui::Component component_;
    bool busy_ = false;
    std::string model_;
    std::string mode_ = "build";
    std::string session_id_;

    ftxui::Element render();
};

}  // namespace ea::tui
