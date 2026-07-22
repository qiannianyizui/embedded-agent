#include "TuiApp.h"
namespace ea::tui {
AgentLoop::OutputFn TuiApp::output_fn() {
    return [](const std::string&) {};
}
AgentLoop::StreamFn TuiApp::stream_fn() {
    return [](const ea::StreamChunk&) {};
}
security::IApprovalHandler* TuiApp::approval_handler() {
    return nullptr;
}
void TuiApp::run(AgentLoop&) {}
}
