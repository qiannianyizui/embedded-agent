// TuiEventListener — bridges AgentEvent to FTXUI UI updates via post_fn
#include "TuiEventListener.h"
#include "ChatArea.h"
#include "StatusBar.h"
#include "TopBar.h"
#include "FormatUtils.h"

namespace ea::tui {

TuiEventListener::TuiEventListener(ChatArea& chat_area, StatusBar& status_bar,
                                   TopBar& top_bar,
                                   std::function<void(std::function<void()>)> post_fn)
    : chat_area_(chat_area), status_bar_(status_bar), top_bar_(top_bar),
      post_fn_(std::move(post_fn)) {}

void TuiEventListener::on_event(const ea::agent::AgentEvent& event) {
    switch (event.type) {
        case ea::agent::AgentEventType::TurnStart:
            post_fn_([this] {
                status_bar_.set_busy(true);
                top_bar_.set_busy(true);
                if (on_turn_start_) on_turn_start_();
            });
            break;

        case ea::agent::AgentEventType::LLMRequest:
            // LLM request is trace data — no TUI action needed
            break;

        case ea::agent::AgentEventType::LLMResponse:
            post_fn_([this, usage = event.usage] {
                status_bar_.update_usage(usage.input_tokens, usage.output_tokens);
            });
            break;

        case ea::agent::AgentEventType::ToolCallStart:
            post_fn_([this, name = event.tool_name, args = event.tool_arguments.dump()] {
                chat_area_.append_tool_start(name, args);
                status_bar_.set_busy(true, tool_verb(name));
                top_bar_.set_busy(true, tool_verb(name));
            });
            break;

        case ea::agent::AgentEventType::ToolCallEnd:
            post_fn_([this, name = event.tool_name, result = event.tool_result,
                      is_err = event.tool_error] {
                chat_area_.append_tool_end(name, result, is_err);
            });
            break;

        case ea::agent::AgentEventType::TurnEnd:
            post_fn_([this, turn_usage = event.turn_usage, turn_cost = event.turn_cost] {
                status_bar_.set_busy(false);
                top_bar_.set_busy(false);
                // Update usage from turn_usage if available
                if (turn_usage.input_tokens > 0 || turn_usage.output_tokens > 0) {
                    status_bar_.update_usage(
                        turn_usage.input_tokens, turn_usage.output_tokens);
                }
                // Update cost from turn_cost if available
                if (turn_cost.total() > 0.0) {
                    status_bar_.update_cost(turn_cost.total());
                }
                if (on_turn_end_) on_turn_end_();
            });
            break;

        case ea::agent::AgentEventType::Error:
            post_fn_([this, msg = event.error_message] {
                chat_area_.append_error(msg);
                status_bar_.set_busy(false);
                top_bar_.set_busy(false);
                if (on_turn_end_) on_turn_end_();
            });
            break;

        case ea::agent::AgentEventType::Interrupt:
            post_fn_([this] {
                chat_area_.append_error("Interrupted");
                status_bar_.set_busy(false);
                top_bar_.set_busy(false);
                if (on_turn_end_) on_turn_end_();
            });
            break;

        case ea::agent::AgentEventType::ModeChanged:
            post_fn_([this, mode = event.mode] {
                status_bar_.set_mode(mode);
                top_bar_.set_mode(mode);
                chat_area_.append_system(mode == "plan"
                    ? "Plan mode: read-only. Planning started."
                    : "Plan approved — switched to build mode.");
            });
            break;
    }
}

}  // namespace ea::tui
