// auto_resume — shared conversation resume logic for CLI and TUI modes
#pragma once
#include "agent/AgentLoop.h"

namespace ea::app {

struct AppContext;  // forward declare to avoid heavy include

// Resume the last conversation if auto_resume is enabled.
// silent=true suppresses the "Resumed: ..." message (TUI handles its own display).
void auto_resume(AppContext& ctx, ea::agent::AgentLoop& loop, bool silent = false);

}  // namespace ea::app
