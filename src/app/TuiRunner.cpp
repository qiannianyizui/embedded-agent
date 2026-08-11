#include "app/TuiRunner.h"
#include "app/AppContext.h"
#include "app/auto_resume.h"
#include "app/onboarding.h"
#include "agent/AgentLoop.h"
#include "agent/LoggingEventListener.h"
#include "tui/TuiApp.h"
#include "log/Logger.h"

namespace ea::app {

int TuiRunner::run(AppContext& ctx) {
    ea::tui::TuiApp tui;

    tui.set_model(ctx.config.provider.default_model.empty()
                      ? ctx.config.agent.model
                      : ctx.config.provider.default_model);
    tui.set_skills(ctx.skills.get());

    auto tui_output = tui.output_fn();
    auto tui_stream = tui.stream_fn();

    ea::agent::AgentLoop tui_loop(
        ctx.effective_provider, ctx.registry.get(), ctx.memory.get(),
        ea::agent::AgentLoop::Config{
            ctx.config.agent.max_iterations, 65536, true,
            ctx.config.agent.stream, ctx.config.conversation.auto_persist,
            ctx.config.provider.default_model.empty()
                ? ctx.config.agent.model : ctx.config.provider.default_model,
            ctx.soul,
            ctx.context_files,
            ctx.skills_index,
            ""
        },
        tui_output,
        tui_stream,
        ctx.security.get(),
        tui.approval_handler(),
        ctx.compressor.get(),
        ctx.memory_strategy.get(),
        ctx.conversation_store.get(),
        ctx.budget_tracker.get(),
        ctx.curated_memory.get()
    );

    if (ctx.debug) {
        tui_loop.add_listener(std::make_shared<ea::agent::LoggingEventListener>());
    }
    // Note: TuiApp has its own TuiEventListener that renders usage/cost in the
    // status bar, so BudgetEventListener is intentionally NOT added here
    // (it would print raw [Usage: ...] lines to stdout and corrupt the FTXUI render).

    auto_resume(ctx, tui_loop, true);

    // First-run profile-build onboarding (Hermes-style, one-time latch).
    std::string onboarding_directive;
    if (ea::app::should_offer_profile(ctx.config)) {
        bool has_sessions = false;
        if (ctx.conversation_store) {
            auto recent = ctx.conversation_store->list(1, 0);
            has_sessions = recent.ok() && !recent.value().empty();
        }
        bool user_profile_empty =
            !ctx.curated_memory || ctx.curated_memory->empty(
                ea::memory::MemoryTarget::User);
        if (!has_sessions && user_profile_empty) {
            onboarding_directive = ea::app::profile_build_directive();
            ea::app::mark_profile_offered(ctx.config);
        }
    }

    tui_loop.set_onboarding_directive(onboarding_directive);

    tui.run(tui_loop, ctx.budget_tracker.get(), ctx.conversation_store.get());
    return 0;
}

}  // namespace ea::app
