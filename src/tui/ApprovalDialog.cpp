// ApprovalDialog — modal overlay implementation
#include "ApprovalDialog.h"
#include "Theme.h"
#include "FormatUtils.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>

namespace ea::tui {

namespace {

bool is_high_risk_tool(const std::string& name) {
    static const char* high_risk_tools[] = {
        "shell", "execute_code", "run_command", "delete_file", "terminal",
    };
    for (const auto& t : high_risk_tools) {
        if (name.find(t) != std::string::npos) return true;
    }
    return false;
}

ftxui::Component make_action_button(const std::string& label,
                                    ftxui::Color color,
                                    std::function<void()> on_click) {
    ftxui::ButtonOption option;
    option.transform = [color](const ftxui::EntryState& s) {
        auto elem = ftxui::text(s.label);
        if (s.focused || s.active) {
            elem = elem | ftxui::bold | ftxui::bgcolor(color)
                       | ftxui::color(ftxui::Color::RGB(13, 17, 23));
        } else {
            elem = elem | ftxui::color(color);
        }
        return elem;
    };
    return ftxui::Button(label, std::move(on_click), option);
}

}  // anonymous namespace

ApprovalDialog::ApprovalDialog(TuiApprovalHandler& handler)
    : handler_(handler) {}

ftxui::Component ApprovalDialog::component() {
    if (!component_) {
        auto& theme = default_theme();

        auto btn_approve = make_action_button(
            " [y] Approve ", theme.color.ok, [this] {
                handler_.set_decision(ea::security::ApprovalDecision::Approved);
            });
        auto btn_reject = make_action_button(
            " [n] Reject ", theme.color.error, [this] {
                handler_.set_decision(ea::security::ApprovalDecision::Rejected);
            });
        auto btn_abort = make_action_button(
            " [a] Abort ", theme.color.muted, [this] {
                handler_.set_decision(ea::security::ApprovalDecision::Aborted);
            });

        auto buttons = ftxui::Container::Horizontal({
            btn_approve, btn_reject, btn_abort
        });

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

    bool dangerous = is_high_risk_tool(req->tool_name);
    std::string args_str = req->arguments.dump(2);

    Color risk_color = dangerous ? theme.color.warn : theme.color.accent;
    std::string risk_label =
        dangerous ? "  ⚠ High-risk tool call " : "  ⚡ Tool call needs approval ";

    std::vector<Element> body;
    body.push_back(hbox({
        text(risk_label) | color(risk_color) | bold,
        filler(),
    }));
    body.push_back(separator());

    // Tool + description
    body.push_back(hbox({
        text("  tool  ") | color(theme.color.muted) | dim,
        text(req->tool_name) | color(theme.color.text) | bold,
    }));
    if (!req->description.empty()) {
        body.push_back(hbox({
            text("  why   ") | color(theme.color.muted) | dim,
            text(truncateFront(req->description, 52))
                | color(theme.color.muted),
        }));
    }
    body.push_back(text(""));

    // Arguments in a code block
    body.push_back(vbox({
                       hbox({
                           text("  arguments ") | color(theme.color.muted) | dim,
                           filler(),
                       }),
                       text(args_str) | color(theme.color.text) | dim,
                   })
                   | borderRounded | color(theme.color.border_soft)
                   | bgcolor(theme.color.tool_bg));
    body.push_back(text(""));

    // Actions
    body.push_back(hbox({
        filler(),
        component_->Render(),
        filler(),
    }));

    Element card = vbox(body) | bgcolor(theme.color.surface);
    if (dangerous) {
        return card | borderDouble | color(theme.color.warn) | center;
    }
    return card | borderRounded | color(theme.color.border) | center;
}

bool ApprovalDialog::on_event(ftxui::Event event) {
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
