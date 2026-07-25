#include "app/TuiRunner.h"
#include "app/AppContext.h"
#include "app/auto_resume.h"
#include "agent/AgentLoop.h"
#include "agent/LoggingEventListener.h"
#include "tui/TuiApp.h"
#include "common/io/Logger.h"

namespace ea::app {

int TuiRunner::run(AppContext& ctx) {
    ea::tui::TuiApp tui;

    auto tui_output = tui.output_fn();
    auto tui_stream = tui.stream_fn();

    ea::agent::AgentLoop tui_loop(
        ctx.effective_provider, ctx.registry.get(), ctx.memory.get(),
        ea::agent::AgentLoop::Config{
            ctx.config.agent.max_iterations, 65536, 100, true,
            ctx.config.agent.stream, ctx.config.conversation.auto_persist
        },
        tui_output,
        tui_stream,
        ctx.security.get(),
        tui.approval_handler(),
        ctx.compressor.get(),
        ctx.memory_strategy.get(),
        ctx.conversation_store.get(),
        ctx.budget_tracker.get()
    );

    if (ctx.debug) {
        tui_loop.add_listener(std::make_shared<ea::agent::LoggingEventListener>());
    }
    // Note: TuiApp has its own TuiEventListener that renders usage/cost in the
    // status bar, so BudgetEventListener is intentionally NOT added here
    // (it would print raw [Usage: ...] lines to stdout and corrupt the FTXUI render).

    auto_resume(ctx, tui_loop, true);

    tui.run(tui_loop, ctx.budget_tracker.get(), ctx.conversation_store.get());
    return 0;
}

}  // namespace ea::app
