#include "app/IRunner.h"
#include "app/CliRunner.h"
#include "ea/build_config.h"

// Task 8 will create ServerRunner.h / TuiRunner.h and define these guards
#if defined(EA_MODE_SERVER) && defined(EA_HAS_SERVER_RUNNER)
#include "app/ServerRunner.h"
#elif defined(EA_ENABLE_TUI) && defined(EA_HAS_TUI_RUNNER)
#include "app/TuiRunner.h"
#endif

namespace ea::app {

std::unique_ptr<IRunner> IRunner::create() {
#if defined(EA_MODE_SERVER) && defined(EA_HAS_SERVER_RUNNER)
    return std::make_unique<ServerRunner>();
#elif defined(EA_ENABLE_TUI) && defined(EA_HAS_TUI_RUNNER)
    return std::make_unique<TuiRunner>();
#else
    return std::make_unique<CliRunner>();
#endif
}

}  // namespace ea::app
