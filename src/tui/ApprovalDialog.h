// ApprovalDialog — FTXUI Modal component for dangerous tool call approval
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
    bool approved_clicked_ = false;
    bool rejected_clicked_ = false;
    bool aborted_clicked_ = false;

    bool on_event(ftxui::Event event);
};

}  // namespace ea::tui
