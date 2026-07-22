// ApprovalDialog — implementation
#include "ApprovalDialog.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>

namespace ea::tui {

ApprovalDialog::ApprovalDialog(TuiApprovalHandler& handler)
    : handler_(handler) {}

ftxui::Component ApprovalDialog::component() {
    if (!component_) {
        auto btn_approve = ftxui::Button("[y] Approve", [this] {
            handler_.set_decision(ea::security::ApprovalDecision::Approved);
        }, ftxui::ButtonOption::Ascii());

        auto btn_reject = ftxui::Button("[n] Reject", [this] {
            handler_.set_decision(ea::security::ApprovalDecision::Rejected);
        }, ftxui::ButtonOption::Ascii());

        auto btn_abort = ftxui::Button("[a] Abort", [this] {
            handler_.set_decision(ea::security::ApprovalDecision::Aborted);
        }, ftxui::ButtonOption::Ascii());

        auto buttons = ftxui::Container::Horizontal({
            btn_approve, btn_reject, btn_abort
        });

        // Wrap with CatchEvent to handle y/n/a keyboard shortcuts
        auto with_events = buttons
            | ftxui::CatchEvent(std::function<bool(ftxui::Event)>(
                  [this](ftxui::Event event) {
                      return on_event(event);
                  }));

        component_ = ftxui::Renderer(with_events, [this] {
            return render();
        });
    }
    return component_;
}

ftxui::Element ApprovalDialog::render() {
    using namespace ftxui;

    auto* req = handler_.current_request();
    if (!req) {
        return text("");
    }

    std::string args_str = req->arguments.dump(2);

    auto content = vbox({
        text("  Dangerous tool call") | bold | color(Color::Red),
        text(""),
        text("  Tool: " + req->tool_name) | bold,
        text("  Args: " + args_str) | dim,
        text("  Desc: " + req->description) | dim,
        separator(),
        hbox({
            component_->Render() | center,
        }),
    });

    return window(text(" Approval Required "), clear_under(content))
        | border
        | center;
}

bool ApprovalDialog::on_event(ftxui::Event event) {
    // Handle y/n/a keyboard shortcuts
    if (event == ftxui::Event::Character('y') ||
        event == ftxui::Event::Character('Y')) {
        handler_.set_decision(ea::security::ApprovalDecision::Approved);
        return true;
    }
    if (event == ftxui::Event::Character('n') ||
        event == ftxui::Event::Character('N')) {
        handler_.set_decision(ea::security::ApprovalDecision::Rejected);
        return true;
    }
    if (event == ftxui::Event::Character('a') ||
        event == ftxui::Event::Character('A')) {
        handler_.set_decision(ea::security::ApprovalDecision::Aborted);
        return true;
    }
    return false;
}

}  // namespace ea::tui
