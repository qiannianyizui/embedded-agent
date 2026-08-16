// ApprovalDialog — modal overlay for tool-call approval
#pragma once
#include "TuiApprovalHandler.h"
#include <ftxui/component/component.hpp>
#include <string>

namespace ea::tui {

class ApprovalDialog {
public:
    explicit ApprovalDialog(TuiApprovalHandler& handler);

    /// Returns the FTXUI component for this dialog (Modal overlay).
    ftxui::Component component();

    /// Returns the rendered FTXUI element (for direct embedding or testing).
    ftxui::Element render();

private:
    TuiApprovalHandler& handler_;
    ftxui::Component component_;
    ftxui::Component buttons_;

    bool on_event(ftxui::Event event);
};

}  // namespace ea::tui
