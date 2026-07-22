#pragma once
#include "agent/AgentLoop.h"
namespace ea::tui {
using AgentLoop = agent::AgentLoop;
class TuiApp {
public:
    AgentLoop::OutputFn output_fn();
    AgentLoop::StreamFn stream_fn();
    security::IApprovalHandler* approval_handler();
    void run(AgentLoop& loop);
};
}
