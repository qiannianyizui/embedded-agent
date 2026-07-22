// TuiEventListener — bridges AgentEvent to FTXUI UI updates via post_fn
#include "TuiEventListener.h"
#include "ChatArea.h"
#include "StatusBar.h"

namespace ea::tui {

TuiEventListener::TuiEventListener(ChatArea& chat_area, StatusBar& status_bar,
                                   std::function<void(std::function<void()>)> post_fn)
    : chat_area_(chat_area), status_bar_(status_bar), post_fn_(std::move(post_fn)) {}

void TuiEventListener::on_event(const ea::agent::AgentEvent& event) {
    switch (event.type) {
        case ea::agent::AgentEventType::TurnStart:
            post_fn_([this] { status_bar_.set_busy(true); });
            break;

        case ea::agent::AgentEventType::LLMResponse:
            post_fn_([this, usage = event.usage] {
                status_bar_.update_usage(usage.input_tokens, usage.output_tokens);
            });
            break;

        case ea::agent::AgentEventType::ToolCallStart:
            post_fn_([this, name = event.tool_name, args = event.tool_arguments.dump()] {
                chat_area_.append_tool_start(name, args);
            });
            break;

        case ea::agent::AgentEventType::ToolCallEnd:
            post_fn_([this, name = event.tool_name, result = event.tool_result,
                      is_err = event.tool_error] {
                chat_area_.append_tool_end(name, result, is_err);
            });
            break;

        case ea::agent::AgentEventType::TurnEnd:
            post_fn_([this] { status_bar_.set_busy(false); });
            break;

        case ea::agent::AgentEventType::Error:
            post_fn_([this, msg = event.error_message] {
                chat_area_.append_error(msg);
            });
            break;

        case ea::agent::AgentEventType::Interrupt:
            post_fn_([this] {
                chat_area_.append_error("Interrupted");
                status_bar_.set_busy(false);
            });
            break;
    }
}

}  // namespace ea::tui
