#include "app/auto_resume.h"
#include "app/AppContext.h"
#include <iostream>

namespace ea::app {

void auto_resume(AppContext& ctx, ea::agent::AgentLoop& loop, bool silent) {
    if (!ctx.conversation_store || !ctx.config.conversation.auto_resume) return;

    auto recent = ctx.conversation_store->list(1, 0);
    if (!recent.ok() || recent.value().empty()) return;

    auto& meta = recent.value()[0];
    auto msgs = ctx.conversation_store->load(meta.id);
    if (!msgs.ok() || msgs.value().empty()) return;

    loop.restore_conversation(meta.id, std::move(msgs.value()));
    if (!silent) {
        std::cout << "Resumed: " << meta.title
                  << " (" << meta.message_count << " messages)" << std::endl;
    }
}

}  // namespace ea::app
