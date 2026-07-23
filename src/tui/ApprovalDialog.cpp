// ApprovalDialog — implementation with Hermes color scheme
#include "ApprovalDialog.h"
#include "Theme.h"
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
    auto& theme = default_theme();

    auto* req = handler_.current_request();
    if (!req) {
        return text("");
    }

    std::string args_str = req->arguments.dump(2);

    // All approval requests are potentially dangerous — use warn styling
    // Check if the tool name suggests high risk (shell, execute, delete, etc.)
    static const char* high_risk_tools[] = {
        "shell", "execute_code", "run_command", "delete_file", "terminal"
    };
    bool dangerous = false;
    for (const auto& t : high_risk_tools) {
        if (req->tool_name.find(t) != std::string::npos) {
            dangerous = true;
            break;
        }
    }

    auto content = vbox({
        text(dangerous ? "  ⚠  Dangerous tool call" : "  ⚡  Tool call requires approval")
            | bold | color(dangerous ? theme.color.warn : theme.color.accent),
        text(""),
        text("  Tool: " + req->tool_name) | bold | color(theme.color.text),
        text("  Args: " + args_str) | color(theme.color.muted) | dim,
        text("  Desc: " + req->description) | color(theme.color.muted) | dim,
        separator(),
        hbox({
            component_->Render() | center,
        }),
    });

    // Dangerous: double border + warn color; normal: rounded border + border color
    if (dangerous) {
        return window(text(" Approval Required ") | color(theme.color.warn),
                      clear_under(content))
            | borderDouble | color(theme.color.warn) | center;
    } else {
        return window(text(" Approval Required ") | color(theme.color.border),
                      clear_under(content))
            | borderRounded | color(theme.color.border) | center;
    }
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
